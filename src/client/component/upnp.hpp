#pragma once

#include <string>

namespace upnp
{
	// Request a UPnP mapping for the bound game port if none exists yet (main thread only).
	void ensure_mapped();

	// Rendezvous-observed "ip:port"; a WAN-IP mismatch proves the mapping useless and drops it (main thread only).
	void on_public_endpoint(const std::string& endpoint);
}
