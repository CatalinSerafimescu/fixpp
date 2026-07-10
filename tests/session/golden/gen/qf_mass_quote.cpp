// QuickFIX-cpp golden-authoring generator for i (MassQuote) —
// 067-codegen-writer-emitter T018 (research.md R5 — deep-nested-group
// insurance: QuotSetGrp NoQuoteSets(296) delimiter QuoteSetID(302), nested
// QuotEntryGrp NoQuoteEntries(295) delimiter QuoteEntryID(299), BOTH
// Required at their own level).
// OFFLINE TOOLING, not checked in / not compiled by the main build.
#include <iostream>
#include <quickfix/fix44/MassQuote.h>

int main() {
    FIX44::MassQuote mq(FIX::QuoteID("QID-100"));

    FIX44::MassQuote::NoQuoteSets set;
    set.set(FIX::QuoteSetID("QS1"));
    set.set(FIX::TotNoQuoteEntries(1));

    FIX44::MassQuote::NoQuoteSets::NoQuoteEntries entry;
    entry.set(FIX::QuoteEntryID("QE1"));
    entry.set(FIX::BidPx(10.5));
    entry.set(FIX::OfferPx(10.75));
    set.addGroup(entry);

    mq.addGroup(set);

    mq.getHeader().setField(FIX::BeginString("FIX.4.4"));
    mq.getHeader().setField(FIX::SenderCompID("S"));
    mq.getHeader().setField(FIX::TargetCompID("T"));
    mq.getHeader().setField(FIX::MsgSeqNum(1));
    mq.getHeader().setField(FIX::SendingTime(FIX::UtcTimeStamp(0, 0, 0, 1, 1, 2024)));

    std::string wire = mq.toString();
    for (char c : wire) std::cout << (c == '\x01' ? '|' : c);
    std::cout << "\n";
    return 0;
}
