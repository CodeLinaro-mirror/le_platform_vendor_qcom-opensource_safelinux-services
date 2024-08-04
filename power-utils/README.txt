# Check Inhibitors

There may be cases where a suspend operation is requested, but a
userspace service is not able to successfully prepare for suspend. In
this case, with this change, the suspend operation is cancelled.

This allows us to fail the suspend operation fast in userspace, rather
than triggering suspend and causing confusing / hard-to-debug behavior
downstream in the suspend operation.

## Implementation

After initiating a systemd suspend operation and before systemd tells
Linux kernel to suspend, all sleep delay inhibitor locks are supposed to
be released by userspace services.

If one or more sleep delay inhibitor locks are not released after
InhibitDelayMaxSec seconds, systemd-logind activates suspend.target,
which activates systemd-suspend.service.

This change adds a service Before= and RequiredBy= systemd-suspend.service that checks if
any sleep delay inhibitor locks are held. If any are still held (meaning
the InhibitDelayMaxSec timeout occurred), then check-inhibitors.service
fails, and systemd-suspend.service is not started. This cancels the
suspend operation.
