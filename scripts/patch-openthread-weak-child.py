#!/usr/bin/env python3
"""Let a weak Thread child rejoin after the gateway restarts.

OpenThread's parent response is delayed by up to a second, and every new
Parent Request mints a new challenge. At about -90 dBm the child hears the
late response, then retries, and the Child ID Request no longer matches.
The first parent scan also ignores anything short of link quality 3.

This edits the ESP-IDF OpenThread sources in place. It is safe to run again.
"""

import os
import sys

MARKER = "icc-weak-child"
IDF = os.environ.get("IDF_PATH", "")
if not IDF:
    sys.exit("IDF_PATH is not set")

CHILD = os.path.join(
    IDF, "components/openthread/openthread/src/core/thread/child.hpp"
)
ROUTER = os.path.join(
    IDF, "components/openthread/openthread/src/core/thread/mle_router.cpp"
)

CHILD_OLD = """    /**
     * Generates a new challenge value to use during a child attach.
     */
    void GenerateChallenge(void) { mAttachChallenge.GenerateRandom(); }
"""

CHILD_NEW = """    /**
     * A cleared child has an all-zero challenge. Attach responses reuse it
     * until a Child ID Request completes, so a retry still matches.
     */
    bool HasAttachChallenge(void) const
    {
        const uint8_t *bytes = reinterpret_cast<const uint8_t *>(&mAttachChallenge);

        for (uint8_t i = 0; i < sizeof(mAttachChallenge); ++i)
        {
            if (bytes[i] != 0)
            {
                return true;
            }
        }
        return false;
    }

    /**
     * Generates a new challenge value to use during a child attach.
     */
    void GenerateChallenge(void) { mAttachChallenge.GenerateRandom(); }
"""

ROUTER_OLD = """    aChild.GenerateChallenge();
    SuccessOrExit(error = message->AppendChallengeTlv(aChild.GetChallenge()));
    SuccessOrExit(error = message->AppendLinkMarginTlv(aChild.GetLinkInfo().GetLinkMargin()));
    SuccessOrExit(error = message->AppendConnectivityTlv());
    SuccessOrExit(error = message->AppendVersionTlv());

    destination.SetToLinkLocalAddress(aChild.GetExtAddress());

    delay = 1 + Random::NonCrypto::GetUint16InRange(0, aRoutersOnlyRequest ? kParentResponseMaxDelayRouters
                                                                           : kParentResponseMaxDelayAll);
"""

ROUTER_NEW = """    // icc-weak-child: keep one challenge for this attach and answer at once.
    // A delayed second response used to replace the challenge while the first
    // was still being retried, so the Child ID Request failed as Security.
    if (!aChild.HasAttachChallenge())
    {
        aChild.GenerateChallenge();
    }
    SuccessOrExit(error = message->AppendChallengeTlv(aChild.GetChallenge()));
    {
        uint8_t margin = aChild.GetLinkInfo().GetLinkMargin();

        // Link quality 3 needs a margin above 20. A child we can already hear
        // at the edge of range would otherwise be ignored on the first scan.
        if (margin >= 2 && margin < 24)
        {
            margin = 24;
        }
        SuccessOrExit(error = message->AppendLinkMarginTlv(margin));
    }
    SuccessOrExit(error = message->AppendConnectivityTlv());
    SuccessOrExit(error = message->AppendVersionTlv());

    destination.SetToLinkLocalAddress(aChild.GetExtAddress());

    OT_UNUSED_VARIABLE(aRoutersOnlyRequest);
    // One millisecond is inside the child's transmit-to-receive turnaround,
    // so the only response is missed. A later retry within 700 ms is dropped
    // as a duplicate and the child never attaches.
    delay = 80;
"""


def patch(path, old, new, label):
    with open(path, "r", encoding="utf-8") as handle:
        text = handle.read()
    if MARKER in text and label == "router":
        return
    if "HasAttachChallenge" in text and label == "child":
        return
    if old not in text:
        sys.exit(f"{path} does not contain the expected {label} text")
    text = text.replace(old, new, 1)
    with open(path, "w", encoding="utf-8") as handle:
        handle.write(text)
    print(f"patched {path}")


def main():
    for path in (CHILD, ROUTER):
        if not os.path.isfile(path):
            sys.exit(f"missing {path}")
    patch(CHILD, CHILD_OLD, CHILD_NEW, "child")
    patch(ROUTER, ROUTER_OLD, ROUTER_NEW, "router")


if __name__ == "__main__":
    main()
