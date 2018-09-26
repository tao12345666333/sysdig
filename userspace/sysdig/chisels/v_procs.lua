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
	id = "procs",
	name = "Processes",
	description = "This is the typical top/htop process list, showing usage of resources like CPU, memory, disk and network on a by process basis.",
	tips = {"This is a perfect view to start a drill down session. Click enter or double click on a process to dive into it and explore its behavior."},
	tags = {"Default", "wsysdig"},
	view_type = "table",
	filter = "evt.type!=switch",
	applies_to = {"", "container.id", "fd.name", "fd.containername", "fd.sport", "fd.sproto", "evt.type", "fd.directory", "fd.containerdirectory", "fd.type", "k8s.pod.id", "k8s.rc.id", "k8s.rs.id", "k8s.svc.id", "k8s.ns.id", "marathon.app.id", "marathon.group.name", "mesos.task.id", "mesos.framework.name"},
	is_root = true,
	drilldown_target = "threads",
	use_defaults = true,
	columns = 
	{
		{
			name = "NA",
			field = "thread.tid",
			is_key = true
		},
		{
			name = "NA",
			field = "proc.pid",
			is_groupby_key = true
		},
		{
			name = "PID",
			description = "Process PID.",
			field = "proc.pid",
			colsize = 7,
		},
		{
			tags = {"containers"},
			name = "VPID",
			field = "proc.vpid",
			description = "PID that the process has inside the container.",
			colsize = 8,
		},
		{
			name = "CPU",
			field = "thread.cpu",
			description = "Amount of CPU used by the proccess.",
			aggregation = "AVG",
			groupby_aggregation = "SUM",
			colsize = 8,
			is_sorting = true
		},
		{
			name = "USER",
			field = "user.name",
			colsize = 12
		},
		{
			name = "TH",
			field = "proc.nthreads",
			description = "Number of threads that the process contains.",
			aggregation = "MAX",
			groupby_aggregation = "MAX",
			colsize = 5
		},
		{
			name = "VIRT",
			field = "thread.vmsize.b",
			description = "Total virtual memory for the process.",
			aggregation = "MAX",
			groupby_aggregation = "MAX",
			colsize = 9
		},
		{
			name = "RES",
			field = "thread.vmrss.b",
			description = "Resident non-swapped memory for the process.",
			aggregation = "MAX",
			groupby_aggregation = "MAX",
			colsize = 9
		},
		{
			name = "FILE",
			field = "evt.buflen.file",
			description = "Total (input+output) file I/O bandwidth generated by the process, in bytes per second.",
			aggregation = "TIME_AVG",
			groupby_aggregation = "SUM",
			colsize = 8
		},
		{
			name = "NET",
			field = "evt.buflen.net",
			description = "Total (input+output) network I/O bandwidth generated by the process, in bytes per second.",
			aggregation = "TIME_AVG",
			groupby_aggregation = "SUM",
			colsize = 8
		},
		{
			tags = {"containers"},
			name = "CONTAINER",
			field = "container.name",
			colsize = 20
		},
		{
			name = "Command",
			description = "The full command line of the process.",
			field = "proc.exeline",
			aggregation = "MAX",
			colsize = 0
		}
	},
	actions = 
	{
		{
			hotkey = "9",
			command = "kill -9 %proc.pid",
			description = "kill -9",
			ask_confirmation = true,
			wait_finish = false
		},
		{
			hotkey = "c",
			command = "gcore %proc.pid",
			description = "generate core",
		},
		{
			hotkey = "g",
			command = "gdb -p %proc.pid",
			description = "gdb attach",
			wait_finish = false
		},
		{
			hotkey = "k",
			command = "kill %proc.pid",
			description = "kill",
			ask_confirmation = true,
			wait_finish = false
		},
		{
			hotkey = "l",
			command = "ltrace -p %proc.pid",
			description = "ltrace",
		},
		{
			hotkey = "s",
			command = "gdb -p %proc.pid --batch --quiet -ex \"thread apply all bt full\" -ex \"quit\"",
			description = "print stack",
		},
		{
			hotkey = "f",
			command = "lsof -p %proc.pid",
			description = "one-time lsof",
		},
		{
			hotkey = "[",
			command = "renice $(expr $(ps -h -p %proc.pid -o nice) + 1) -p %proc.pid",
			description = "increment nice by 1",
			wait_finish = false,
		},
		{
			hotkey = "]",
			command = "renice $(expr $(ps -h -p %proc.pid -o nice) - 1) -p %proc.pid",
			description = "decrement nice by 1",
			wait_finish = false,
		},
	},
}
