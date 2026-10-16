#pragma once
class CKeySync
{
public:
	static void ApplyNetworkPlayerContext(CNetworkPlayer* player);
	static void ApplyLocalContext();
	static void CollectState(Packets::Players::SKeySnapshot& keySnapshot);

	// private build: while one exists, CollectState reports every key released, as it does while the player's controls
	// are disabled. CNetworkKeepAlive sends the player's last update that way when the game goes to the background.
	class CReleasedKeysScope
	{
	public:
		CReleasedKeysScope() { ms_nReleasedKeysScopes++; }
		~CReleasedKeysScope() { ms_nReleasedKeysScopes--; }
		CReleasedKeysScope(const CReleasedKeysScope&) = delete;
		CReleasedKeysScope& operator=(const CReleasedKeysScope&) = delete;
	};

private:
	static inline int ms_nReleasedKeysScopes = 0;
};

