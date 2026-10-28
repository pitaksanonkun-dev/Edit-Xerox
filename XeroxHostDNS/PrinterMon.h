#pragma once
// Checks whether each printer is reachable (non-blocking TCP connect to port 80).
void printerMonLoop();
void printerMonReset();
