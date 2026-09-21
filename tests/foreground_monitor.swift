// Read-only OS-focus observer for isolated browser tests. Never activates apps.
import AppKit
import Darwin

var previous: pid_t = -1
while true {
    let pid = NSWorkspace.shared.frontmostApplication?.processIdentifier ?? -1
    if pid != previous {
        print(pid)
        fflush(stdout)
        previous = pid
    }
    Thread.sleep(forTimeInterval: 0.02)
}
