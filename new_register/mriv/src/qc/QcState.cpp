#include "qc/QcState.hpp"

namespace mriv::qc
{

using mriv::term::KeyResult;

QcState::QcState(std::vector<QC::QCRecord>& records, SaveFn save, bool autoAdvance)
    : records_(records), save_(std::move(save)), autoAdvance_(autoAdvance)
{
}

KeyResult QcState::handleKey(char key)
{
    using namespace mriv::term;

    // A resize is not the user acting, so it must not eat the message they
    // have not had a chance to read yet.
    if (key == kKeyResize)
        return KeyResult::Changed;

    message_.clear();

    if (key == 'q' || key == '\x1b')
    {
        if (dirty_)
            save();
        return KeyResult::Quit;
    }

    if (records_.empty())
        return KeyResult::Ignored;

    switch (key)
    {
    case 'p':
    case 'P':
        return setVerdict(QC::kStatusPass);
    case 'w':
    case 'W':
        return setVerdict(QC::kStatusWarning);
    case 'f':
    case 'F':
        return setVerdict(QC::kStatusFail);
    case 'u':
    case '\b':
    case '\x7f':
        return clearVerdict();

    case kKeyRight:
    case kKeyDown:
    case kKeyPageDown:
    case 'l':
    case 'n':
    case 'j':
    case ' ':
        return index_ + 1 < records_.size() ? moveTo(index_ + 1) : KeyResult::Ignored;
    case kKeyLeft:
    case kKeyUp:
    case kKeyPageUp:
    case 'h':
    case 'b':
    case 'k':
        return index_ > 0 ? moveTo(index_ - 1) : KeyResult::Ignored;
    case kKeyHome:
    case 'g':
        return moveTo(0);
    case kKeyEnd:
    case 'G':
        return moveTo(records_.size() - 1);
    case ']':
        return jumpUnrated(+1);
    case '[':
        return jumpUnrated(-1);

    case 's':
        if (save())
            message_ = "Saved";
        return KeyResult::Changed;
    default:
        return KeyResult::Ignored;
    }
}

QcState::Counts QcState::counts() const
{
    Counts c;
    for (const auto& record : records_)
    {
        if (record.qc_status == QC::kStatusPass)
            ++c.pass;
        else if (record.qc_status == QC::kStatusWarning)
            ++c.warning;
        else if (record.qc_status == QC::kStatusFail)
            ++c.fail;
        else
            ++c.unrated;
    }
    return c;
}

KeyResult QcState::setVerdict(const char* status)
{
    records_[index_].qc_status = status;
    dirty_ = true;
    bool saved = save();

    if (saved && counts().unrated == 0)
        message_ = "All " + std::to_string(records_.size()) + " images rated";

    if (autoAdvance_ && index_ + 1 < records_.size())
        ++index_;
    return KeyResult::Changed;
}

KeyResult QcState::clearVerdict()
{
    if (records_[index_].qc_status.empty())
        return KeyResult::Ignored;

    records_[index_].qc_status.clear();
    dirty_ = true;
    save();
    return KeyResult::Changed;
}

KeyResult QcState::moveTo(size_t index)
{
    if (index == index_)
        return KeyResult::Ignored;
    index_ = index;
    return KeyResult::Changed;
}

KeyResult QcState::jumpUnrated(int direction)
{
    const size_t n = records_.size();
    for (size_t step = 1; step < n; ++step)
    {
        size_t candidate = direction > 0 ? (index_ + step) % n
                                         : (index_ + n - step) % n;
        if (records_[candidate].qc_status.empty())
            return moveTo(candidate);
    }

    // Say so rather than stay silent: a key that does nothing visible reads
    // as a key that did not register.
    message_ = records_[index_].qc_status.empty() ? "No other unrated images"
                                                  : "No unrated images";
    return KeyResult::Changed;
}

bool QcState::save()
{
    if (save_ && save_())
    {
        dirty_ = false;
        return true;
    }
    message_ = "Save failed";
    return false;
}

} // namespace mriv::qc
