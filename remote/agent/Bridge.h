#pragma once

namespace kode::remote
{

// Copies stdin to the daemon's socket and the socket to stdout, starting the
// daemon (`self daemon`) when none answers. Returns the exit code.
int bridge(const char *self);

} // namespace kode::remote
