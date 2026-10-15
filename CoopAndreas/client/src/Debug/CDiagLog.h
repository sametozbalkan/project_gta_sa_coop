#pragma once

// Temporary diagnostics (enabled with --coopdiag): a background thread appends a snapshot of the
// game, network and script state to CoopAndreas_diag.log in the game folder every 2 seconds.
// It keeps writing even if the game loop hangs, which tells a frozen game from a waiting one.
class CDiagLog
{
public:
    static void Start();
};
