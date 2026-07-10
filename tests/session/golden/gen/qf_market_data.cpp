// QuickFIX-cpp golden-authoring generator for W (MarketDataSnapshotFullRefresh)
// and X (MarketDataIncrementalRefresh) — 067-codegen-writer-emitter T018
// (research.md R5 — the REQUIRED W/X paired NoMDEntries(268) discriminator:
// SAME no_tag, delimiter MDEntryType(269) in W vs MDUpdateAction(279) in X).
// OFFLINE TOOLING, not checked in / not compiled by the main build.
#include <iostream>
#include <quickfix/fix44/MarketDataIncrementalRefresh.h>
#include <quickfix/fix44/MarketDataSnapshotFullRefresh.h>

namespace {
void print_body(std::string const& wire) {
    for (char c : wire) std::cout << (c == '\x01' ? '|' : c);
    std::cout << "\n";
}
}  // namespace

int main() {
    // W — MarketDataSnapshotFullRefresh: MDFullGrp NoMDEntries(268) REQUIRED,
    // delimiter MDEntryType(269).
    {
        FIX44::MarketDataSnapshotFullRefresh w;
        w.set(FIX::MDReqID("MDR-W1"));
        w.set(FIX::Symbol("MSFT"));

        FIX44::MarketDataSnapshotFullRefresh::NoMDEntries entry;
        entry.set(FIX::MDEntryType(FIX::MDEntryType_BID));
        entry.set(FIX::MDEntryPx(99.5));
        entry.set(FIX::MDEntrySize(100));
        w.addGroup(entry);

        w.getHeader().setField(FIX::BeginString("FIX.4.4"));
        w.getHeader().setField(FIX::SenderCompID("S"));
        w.getHeader().setField(FIX::TargetCompID("T"));
        w.getHeader().setField(FIX::MsgSeqNum(1));
        w.getHeader().setField(FIX::SendingTime(FIX::UtcTimeStamp(0, 0, 0, 1, 1, 2024)));

        std::cout << "W: ";
        print_body(w.toString());
    }

    // X — MarketDataIncrementalRefresh: MDIncGrp NoMDEntries(268) REQUIRED,
    // delimiter MDUpdateAction(279) — SAME no_tag as W, DIFFERENT delimiter.
    {
        FIX44::MarketDataIncrementalRefresh x;
        x.set(FIX::MDReqID("MDR-X1"));

        FIX44::MarketDataIncrementalRefresh::NoMDEntries entry;
        entry.set(FIX::MDUpdateAction(FIX::MDUpdateAction_NEW));
        entry.set(FIX::MDEntryType(FIX::MDEntryType_BID));
        entry.set(FIX::MDEntryPx(100.25));
        x.addGroup(entry);

        x.getHeader().setField(FIX::BeginString("FIX.4.4"));
        x.getHeader().setField(FIX::SenderCompID("S"));
        x.getHeader().setField(FIX::TargetCompID("T"));
        x.getHeader().setField(FIX::MsgSeqNum(1));
        x.getHeader().setField(FIX::SendingTime(FIX::UtcTimeStamp(0, 0, 0, 1, 1, 2024)));

        std::cout << "X: ";
        print_body(x.toString());
    }
    return 0;
}
