#pragma once

// A single-file portable build carries the Python runtime and the plugin
// scripts as a zip appended to the exe; they are unpacked next to it.
namespace Extera::Runtime {

// True when an appended runtime is present and unpacked and up to date.
[[nodiscard]] bool Ensure();

} // namespace Extera::Runtime
