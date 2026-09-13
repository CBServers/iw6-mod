#pragma once

namespace ipc
{
	// Queues one newline-terminated JSON line for the launcher; dropped if the pipe is down.
	void send_message(std::string line);

	// Push a presence update on the next main-thread frame instead of waiting for the 2s tick.
	void flush_presence();
}
