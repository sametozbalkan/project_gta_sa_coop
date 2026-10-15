#include "stdafx.h"
#include "CDiscordRPC.h"

// COOP_NO_DISCORD_RPC: private build, Discord Rich Presence is compiled out, so no Discord_* call is linked in
void CDiscordRPC::Init()
{
#ifndef COOP_NO_DISCORD_RPC
	DiscordEventHandlers handlers;

	memset(&handlers, 0, sizeof(handlers));
	memset(&presence, 0, sizeof(presence));

	presence.state = state.c_str();
	presence.details = details.c_str();
	presence.startTimestamp = std::time(nullptr);
	presence.largeImageKey = "icon";
	presence.largeImageText = "CoopAndreas";

	Discord_Initialize("1324128258672951317", &handlers, 1, NULL);
	Discord_UpdatePresence(&presence);
#endif
}

void CDiscordRPC::Destroy()
{
#ifndef COOP_NO_DISCORD_RPC
	Discord_Shutdown();
#endif
}

void CDiscordRPC::SetDetailsAndState(std::string details, std::string state)
{
#ifndef COOP_NO_DISCORD_RPC
	CDiscordRPC::details = details;
	presence.details = details.c_str();
	CDiscordRPC::state = state;
	presence.state = state.c_str();
	Discord_UpdatePresence(&presence);
#endif
}