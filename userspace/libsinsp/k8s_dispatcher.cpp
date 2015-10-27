//
// k8s_dispatcher.cpp
//

#include "k8s_dispatcher.h"
#include "sinsp.h"
#include "sinsp_int.h"
#include <assert.h>
#include <stdexcept>
#include <algorithm>
#include <sstream>
#include <iostream>


k8s_dispatcher::k8s_dispatcher(k8s_component::type t, k8s_state_s& state
#ifndef K8S_DISABLE_THREAD
	,std::mutex& mut
#endif
	) :
	m_type(t),
	m_state(state)
#ifndef K8S_DISABLE_THREAD
	,m_mutex(mut)
#endif
{
}

void k8s_dispatcher::enqueue(k8s_event_data&& event_data)
{
	assert(event_data.component() == m_type);

	std::string&& data = event_data.data();

	if(m_messages.size() == 0)
	{
		m_messages.push_back("");
	}

	std::string* msg = &m_messages.back();
	std::string::size_type pos = msg->find_first_of('\n');
	
	// previous msg full, this is a beginning of new message
	if(pos != std::string::npos && pos == (msg->size() - 1))
	{
		m_messages.push_back("");
		msg = &m_messages.back();
	}

	while ((pos = data.find_first_of('\n')) != std::string::npos)
	{
		msg->append((data.substr(0, pos + 1)));
		data = data.substr(pos + 1);
		m_messages.push_back("");
		msg = &m_messages.back();
	};

	if(data.size() > 0)
	{
		msg->append((data));
	}

	dispatch(); // candidate for separate thread
}

bool k8s_dispatcher::is_valid(const std::string& msg)
{
	// zero-length message is valid because that's how it starts its life.
	// so, here we only check for messages that are single newline only
	// or those that are longer than one character and contain multiple newlines.

	if((msg.size() == 1 && msg[0] == '\n') ||
		std::count(msg.begin(), msg.end(), '\n') > 1)
	{
		return false;
	}
	return true;
}

bool k8s_dispatcher::is_ready(const std::string& msg)
{
	// absurd minimum ( "{}\n" ) but it's hard to tell 
	// what minimal size is, so there ...
	if(msg.size() < 3) 
	{
		return false;
	}
	return msg[msg.size() - 1] == '\n';
}

k8s_dispatcher::msg_data k8s_dispatcher::get_msg_data(const Json::Value& root)
{
	msg_data data;
	Json::Value evtype = root["type"];
	if(!evtype.isNull())
	{
		const std::string& et = evtype.asString();
		if(!et.empty())
		{
			if(et[0] == 'A') { data.m_reason = COMPONENT_ADDED;    }
			else if(et[0] == 'M') { data.m_reason = COMPONENT_MODIFIED; }
			else if(et[0] == 'D') { data.m_reason = COMPONENT_DELETED;  }
			else if(et[0] == 'E') { data.m_reason = COMPONENT_ERROR;    }
		}
		else
		{
			return msg_data();
		}
	}
	Json::Value object = root["object"];
	if(!object.isNull() && object.isObject())
	{
		Json::Value meta = object["metadata"];
		if(!meta.isNull() && meta.isObject())
		{
			Json::Value name = meta["name"];
			if(!name.isNull())
			{
				data.m_name = name.asString();
			}
			Json::Value uid = meta["uid"];
			if(!uid.isNull())
			{
				data.m_uid = uid.asString();
			}
			Json::Value nspace = meta["namespace"];
			if(!nspace.isNull())
			{
				data.m_namespace = nspace.asString();
			}
		}
	}
	return data;
}

void k8s_dispatcher::handle_node(const Json::Value& root, const msg_data& data)
{
	K8S_LOCK_GUARD_MUTEX;

	if(data.m_reason == COMPONENT_ADDED)
	{
		std::vector<std::string> addresses = k8s_component::extract_nodes_addresses(root["status"]);
		if(m_state.has(m_state.get_nodes(), data.m_uid))
		{
			std::ostringstream os;
			os << "ADDED message received for existing node [" << data.m_uid << "], updating only.";
			g_logger.log(os.str(), sinsp_logger::SEV_INFO);
		}
		k8s_node_s& node = m_state.get_component<k8s_state_s::nodes, k8s_node_s>(m_state.get_nodes(), data.m_name, data.m_uid);
		if(addresses.size() > 0)
		{
			node.set_host_ips(std::move(addresses));
		}
		Json::Value object = root["object"];
		if(!object.isNull())
		{
			Json::Value metadata = object["metadata"];
			if(!metadata.isNull())
			{
				k8s_pair_list entries = k8s_component::extract_object(metadata, "labels");
				if(entries.size() > 0)
				{
					node.set_labels(std::move(entries));
				}
			}
		}
	}
	else if(data.m_reason == COMPONENT_MODIFIED)
	{
		std::vector<std::string> addresses = k8s_component::extract_nodes_addresses(root["status"]);
		if(!m_state.has(m_state.get_nodes(), data.m_uid))
		{
			std::ostringstream os;
			os << "MODIFIED message received for non-existing node [" << data.m_uid << "], giving up.";
			g_logger.log(os.str(), sinsp_logger::SEV_ERROR);
			return;
		}
		k8s_node_s& node = m_state.get_component<k8s_state_s::nodes, k8s_node_s>(m_state.get_nodes(), data.m_name, data.m_uid);
		if(addresses.size() > 0)
		{
			node.add_host_ips(std::move(addresses));
		}
		Json::Value object = root["object"];
		if(!object.isNull())
		{
			Json::Value metadata = object["metadata"];
			if(!metadata.isNull())
			{
				k8s_pair_list entries = k8s_component::extract_object(metadata, "labels");
				if(entries.size() > 0)
				{
					node.add_labels(std::move(entries));
				}
			}
		}
	}
	else if(data.m_reason == COMPONENT_DELETED)
	{
		if(!m_state.delete_component(m_state.get_nodes(), data.m_uid))
		{
			g_logger.log(std::string("NODE not found: ") + data.m_name, sinsp_logger::SEV_ERROR);
		}
	}
	else // COMPONENT_ERROR
	{
		g_logger.log("Bad NODE watch message.", sinsp_logger::SEV_ERROR);
	}
}

void k8s_dispatcher::handle_namespace(const Json::Value& root, const msg_data& data)
{
	K8S_LOCK_GUARD_MUTEX;

	if(data.m_reason == COMPONENT_ADDED)
	{
		if(m_state.has(m_state.get_namespaces(), data.m_uid))
		{
			std::ostringstream os;
			os << "ADDED message received for existing namespace [" << data.m_uid << "], updating only.";
			g_logger.log(os.str(), sinsp_logger::SEV_INFO);
		}
		k8s_ns_s& ns = m_state.get_component<k8s_state_s::namespaces, k8s_ns_s>(m_state.get_namespaces(), data.m_name, data.m_uid);
		Json::Value object = root["object"];
		if(!object.isNull())
		{
			Json::Value metadata = object["metadata"];
			if(!metadata.isNull())
			{
				k8s_pair_list entries = k8s_component::extract_object(metadata, "labels");
				if(entries.size() > 0)
				{
					ns.set_labels(std::move(entries));
				}
			}
		}
	}
	else if(data.m_reason == COMPONENT_MODIFIED)
	{
		if(!m_state.has(m_state.get_namespaces(), data.m_uid))
		{
			std::ostringstream os;
			os << "MODIFIED message received for non-existing node [" << data.m_uid << "], giving up.";
			g_logger.log(os.str(), sinsp_logger::SEV_ERROR);
			return;
		}
		k8s_ns_s& ns = m_state.get_component<k8s_state_s::namespaces, k8s_ns_s>(m_state.get_namespaces(), data.m_name, data.m_uid);
		Json::Value object = root["object"];
		if(!object.isNull())
		{
			Json::Value metadata = object["metadata"];
			if(!metadata.isNull())
			{
				k8s_pair_list entries = k8s_component::extract_object(metadata, "labels");
				if(entries.size() > 0)
				{
					ns.add_labels(std::move(entries));
				}
			}
		}
	}
	else if(data.m_reason == COMPONENT_DELETED)
	{
		if(!m_state.delete_component(m_state.get_namespaces(), data.m_uid))
		{
			g_logger.log(std::string("NAMESPACE not found: ") + data.m_name, sinsp_logger::SEV_ERROR);
		}
	}
	else // COMPONENT_ERROR
	{
		g_logger.log("Bad NAMESPACE watch message.", sinsp_logger::SEV_ERROR);
	}
}

void k8s_dispatcher::handle_pod(const Json::Value& root, const msg_data& data)
{
	K8S_LOCK_GUARD_MUTEX;

	if(data.m_reason == COMPONENT_ADDED)
	{
		Json::Value object = root["object"];
		if(!object.isNull())
		{
			if(m_state.has(m_state.get_pods(), data.m_uid))
			{
				std::ostringstream os;
				os << "ADDED message received for existing pod [" << data.m_uid << "], updating only.";
				g_logger.log(os.str(), sinsp_logger::SEV_INFO);
			}
			k8s_pod_s& pod = m_state.get_component<k8s_state_s::pods, k8s_pod_s>(m_state.get_pods(), data.m_name, data.m_uid, data.m_namespace);
			Json::Value metadata = object["metadata"];
			if(!metadata.isNull())
			{
				k8s_pair_list entries = k8s_component::extract_object(metadata, "labels");
				if(entries.size() > 0)
				{
					pod.set_labels(std::move(entries));
				}
			}
			k8s_pod_s::container_list containers = k8s_component::extract_pod_containers(object);
			pod.set_container_ids(std::move(containers));
			k8s_component::extract_pod_data(object, pod);
		}
	}
	else if(data.m_reason == COMPONENT_MODIFIED)
	{
		Json::Value object = root["object"];
		if(!object.isNull())
		{
			if(!m_state.has(m_state.get_pods(), data.m_uid))
			{
				std::ostringstream os;
				os << "MODIFIED message received for non-existing pod [" << data.m_uid << "], giving up.";
				g_logger.log(os.str(), sinsp_logger::SEV_ERROR);
				return;
			}
			k8s_pod_s& pod = m_state.get_component<k8s_state_s::pods, k8s_pod_s>(m_state.get_pods(), data.m_name, data.m_uid, data.m_namespace);
			Json::Value metadata = object["metadata"];
			if(!metadata.isNull())
			{
				k8s_pair_list entries = k8s_component::extract_object(metadata, "labels");
				if(entries.size() > 0)
				{
					pod.add_labels(std::move(entries));
				}
			}
			k8s_pod_s::container_list containers = k8s_component::extract_pod_containers(object);
			pod.add_container_ids(std::move(containers));
			k8s_component::extract_pod_data(object, pod);
		}
	}
	else if(data.m_reason == COMPONENT_DELETED)
	{
		if(!m_state.delete_component(m_state.get_namespaces(), data.m_uid))
		{
			g_logger.log(std::string("POD not found: ") + data.m_name, sinsp_logger::SEV_ERROR);
		}
	}
	else // COMPONENT_ERROR
	{
		g_logger.log("Bad POD watch message.", sinsp_logger::SEV_ERROR);
	}
}

void k8s_dispatcher::handle_rc(const Json::Value& root, const msg_data& data)
{
	K8S_LOCK_GUARD_MUTEX;

	if(data.m_reason == COMPONENT_ADDED)
	{
		if(m_state.has(m_state.get_rcs(), data.m_uid))
		{
			std::ostringstream os;
			os << "ADDED message received for existing replication controller [" << data.m_uid << "], updating only.";
			g_logger.log(os.str(), sinsp_logger::SEV_INFO);
		}
		k8s_rc_s& rc = m_state.get_component<k8s_state_s::controllers, k8s_rc_s>(m_state.get_rcs(), data.m_name, data.m_uid, data.m_namespace);
		Json::Value object = root["object"];
		if(!object.isNull())
		{
			Json::Value metadata = object["metadata"];
			if(!metadata.isNull())
			{
				k8s_pair_list labels = k8s_component::extract_object(metadata, "labels");
				if(labels.size() > 0)
				{
					rc.set_labels(std::move(labels));
				}
			}

			Json::Value spec = object["spec"];
			if(!spec.isNull())
			{
				k8s_pair_list selectors = k8s_component::extract_object(spec, "selector");
				if(selectors.size() > 0)
				{
					rc.set_selectors(std::move(selectors));
				}
			}
		}
	}
	else if(data.m_reason == COMPONENT_MODIFIED)
	{
		if(!m_state.has(m_state.get_rcs(), data.m_uid))
		{
			std::ostringstream os;
			os << "MODIFIED message received for non-existing replication controller [" << data.m_uid << "], giving up.";
			g_logger.log(os.str(), sinsp_logger::SEV_ERROR);
			return;
		}
		k8s_rc_s& rc = m_state.get_component<k8s_state_s::controllers, k8s_rc_s>(m_state.get_rcs(), data.m_name, data.m_uid, data.m_namespace);
		Json::Value object = root["object"];
		if(!object.isNull())
		{
			Json::Value metadata = object["metadata"];
			if(!metadata.isNull())
			{
				k8s_pair_list labels = k8s_component::extract_object(metadata, "labels");
				if(labels.size() > 0)
				{
					rc.add_labels(std::move(labels));
				}
			}

			Json::Value spec = object["spec"];
			if(!spec.isNull())
			{
				k8s_pair_list selectors = k8s_component::extract_object(spec, "selector");
				if(selectors.size() > 0)
				{
					rc.add_selectors(std::move(selectors));
				}
			}
		}
	}
	else if(data.m_reason == COMPONENT_DELETED)
	{
		if(!m_state.delete_component(m_state.get_namespaces(), data.m_uid))
		{
			g_logger.log(std::string("CONTROLLER not found: ") + data.m_name, sinsp_logger::SEV_ERROR);
		}
	}
	else // COMPONENT_ERROR
	{
		g_logger.log("Bad CONTROLLER watch message.", sinsp_logger::SEV_ERROR);
	}
}

void k8s_dispatcher::handle_service(const Json::Value& root, const msg_data& data)
{
	K8S_LOCK_GUARD_MUTEX;

	if(data.m_reason == COMPONENT_ADDED)
	{
		Json::Value object = root["object"];
		if(!object.isNull())
		{
			if(m_state.has(m_state.get_services(), data.m_uid))
			{
				std::ostringstream os;
				os << "ADDED message received for existing service [" << data.m_uid << "], updating only.";
				g_logger.log(os.str(), sinsp_logger::SEV_INFO);
			}
			k8s_service_s& service = m_state.get_component<k8s_state_s::services, k8s_service_s>(m_state.get_services(), data.m_name, data.m_uid, data.m_namespace);
			Json::Value metadata = object["metadata"];
			if(!metadata.isNull())
			{
				k8s_pair_list entries = k8s_component::extract_object(metadata, "labels");
				if(entries.size() > 0)
				{
					service.set_labels(std::move(entries));
				}
			}
			k8s_component::extract_services_data(object, service);
		}
	}
	else if(data.m_reason == COMPONENT_MODIFIED)
	{
		Json::Value object = root["object"];
		if(!object.isNull())
		{
			if(!m_state.has(m_state.get_services(), data.m_uid))
			{
				std::ostringstream os;
				os << "MODIFIED message received for non-existing service [" << data.m_uid << "], giving up.";
				g_logger.log(os.str(), sinsp_logger::SEV_ERROR);
				return;
			}
			k8s_service_s& service = m_state.get_component<k8s_state_s::services, k8s_service_s>(m_state.get_services(), data.m_name, data.m_uid, data.m_namespace);
			Json::Value metadata = object["metadata"];
			if(!metadata.isNull())
			{
				k8s_pair_list entries = k8s_component::extract_object(metadata, "labels");
				if(entries.size() > 0)
				{
					service.add_labels(std::move(entries));
				}
			}
			k8s_component::extract_services_data(object, service);
		}
	}
	else if(data.m_reason == COMPONENT_DELETED)
	{
		if(!m_state.delete_component(m_state.get_namespaces(), data.m_uid))
		{
			g_logger.log(std::string("SERVICE not found: ") + data.m_name, sinsp_logger::SEV_ERROR);
		}
	}
	else // COMPONENT_ERROR
	{
		g_logger.log("Bad SERVICE watch message.", sinsp_logger::SEV_ERROR);
	}
}

void k8s_dispatcher::dispatch()
{
	for (list::iterator it = m_messages.begin(); it != m_messages.end();)
	{
		if(is_ready(*it))
		{
			Json::Value root;
			Json::Reader reader;
			if(reader.parse(*it, root, false))
			{
				std::ostringstream os;
				msg_data data = get_msg_data(root);
				if(data.is_valid())
				{
					std::ostringstream os;
					os << '[' << to_reason_desc(data.m_reason) << ',';
					switch (m_type)
					{
						case k8s_component::K8S_NODES:
							os << "NODE,";
							handle_node(root, data);
							break;
						case k8s_component::K8S_NAMESPACES:
							os << "NAMESPACE,";
							handle_namespace(root, data);
							break;
						case k8s_component::K8S_PODS:
							os << "POD,";
							handle_pod(root, data);
							break;
						case k8s_component::K8S_REPLICATIONCONTROLLERS:
							os << "REPLICATION_CONTROLLER,";
							handle_rc(root, data);
							break;
						case k8s_component::K8S_SERVICES:
							os << "SERVICE,";
							handle_service(root, data);
							break;
						default:
						{
							std::ostringstream eos;
							eos << "Unknown component: " << static_cast<int>(m_type);
							throw sinsp_exception(os.str());
						}
					}
					os << data.m_name << ',' << data.m_uid << ',' << data.m_namespace << ']';
					g_logger.log(os.str(), sinsp_logger::SEV_INFO);
					//g_logger.log(root.toStyledString(), sinsp_logger::SEV_DEBUG);
				}
			}
			else
			{
				// TODO: bad notification - discard or throw?
				g_logger.log("Bad JSON message received.", sinsp_logger::SEV_ERROR);
			}
			it = m_messages.erase(it);
		}
		else
		{
			++it;
		}
	}
}

std::string k8s_dispatcher::to_reason_desc(msg_reason reason)
{
	switch (reason)
	{
	case COMPONENT_ADDED:
		return "ADDED";
	case COMPONENT_MODIFIED:
		return "MODIFIED";
	case COMPONENT_DELETED:
		return "DELETED";
	case COMPONENT_ERROR:
		return "ERROR";
	case COMPONENT_UNKNOWN:
		return "UNKNOWN";
	default:
		return "";
	}
}

k8s_dispatcher::msg_reason k8s_dispatcher::to_reason(const std::string& desc)
{
	if(desc == "ADDED") { return COMPONENT_ADDED; }
	else if(desc == "MODIFIED") { return COMPONENT_MODIFIED; }
	else if(desc == "DELETED") { return COMPONENT_DELETED; }
	else if(desc == "ERROR") { return COMPONENT_ERROR; }
	else if(desc == "UNKNOWN") { return COMPONENT_UNKNOWN; }
	throw sinsp_exception(desc);
}