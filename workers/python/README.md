# Python Worker

`python -m kearne._worker` runs the warm, pinned Python role over inherited
binary pipes. Standard output contains only bounded Protobuf frames; diagnostics
use standard error. The coordinator owns project state and worker lifecycle.
