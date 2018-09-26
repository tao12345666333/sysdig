--[[
Copyright (C) 2013-2018 Draios inc.

This file is part of sysdig.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.

--]]

view_info = 
{
	id = "threads",
	name = "Threads",
	description = "This view lists all the threads running in the system or in the current selection, showing usage of resources like CPU, memory, disk and network for each thread.",
	tips = {"Apply this view to a process to get the list of threads for that process only. Similarly, apply it to a container to see the threads running inside it."},
	tags = {"Default", "wsysdig"},
	view_type = "table",
	filter = "evt.type!=switch",
	applies_to = {"", "proc.pid", "thread.nametid", "proc.name", "container.id", "fd.sport", "fd.sproto", "fd.name", "fd.containername", "fd.directory", "fd.containerdirectory", "evt.res", "k8s.pod.id", "k8s.rc.id", "k8s.rs.id", "k8s.svc.id", "k8s.ns.id", "marathon.app.id", "marathon.group.name", "mesos.task.id", "mesos.framework.name"},
	is_root = true,
	drilldown_target = "files",
	use_defaults = true,
	columns = 
	{
		{
			name = "NA",
			field = "thread.tid",
			is_key = true
		},
		{
			name = "PID",
			description = "PID of the process this thread belongs to.",
			field = "proc.pid",
			colsize = 8,
		},
		{
			name = "TID",
			description = "Thread-specific ID. Main threads have TID=PID.",
			field = "thread.tid",
			colsize = 8,
		},
		{
			tags = {"containers"},
			name = "VPID",
			field = "proc.vpid",
			description = "PID that the process has inside the container.",
			colsize = 8,
		},
		{
			tags = {"containers"},
			name = "VTID",
			field = "thread.vtid",
			description = "TID that the tread has inside the container.",
			colsize = 8,
		},
		{
			name = "CPU",
			field = "thread.cpu",
			description = "Amount of CPU used by the proccess.",
			colsize = 8,
			aggregation = "AVG",
			is_sorting = true
		},
		{
			name = "FILE",
			field = "evt.buflen.file",
			description = "Total (input+output) file I/O bandwidth generated by the thread, in bytes per second.",
			colsize = 8,
			aggregation = "TIME_AVG"
		},
		{
			name = "NET",
			field = "evt.buflen.net",
			description = "Total (input+output) network I/O bandwidth generated by the thread, in bytes per second.",
			colsize = 8,
			aggregation = "TIME_AVG"
		},
		{
			tags = {"containers"},
			name = "Container",
			field = "container.name",
			description = "Name of the container. What this field contains depends on the containerization technology. For example, for docker this is the content of the 'NAMES' column in 'docker ps'",
			colsize = 20
		},
		{
			name = "Command",
			description = "The full command line of the process.",
			field = "proc.exeline",
			aggregation = "MAX",
			colsize = 0
		}
	}
}
