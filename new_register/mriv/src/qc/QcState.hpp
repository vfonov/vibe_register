#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

#include "interactive/Keys.hpp"

#include "CSVHandler.h"

namespace mriv::qc
{

/// Writes the results CSV. Returns false if the save failed.
using SaveFn = std::function<bool()>;

/// The mriv_qc session state: which image is current and what each key does
/// to the verdicts. Driven by mriv::term::runSession(), like mriv's
/// ViewState, and just as free of the terminal -- everything here is
/// unit-tested.
///
/// The records are the CSVHandler's own vector, edited in place, so `save`
/// only has to write them out. Every verdict change is saved at once (as in
/// new_qc): a QC pass over hundreds of images should not be lost to a
/// dropped SSH connection.
class QcState
{
public:
    /// `records` must outlive the state. With `autoAdvance` a verdict moves
    /// on to the next image, as in new_qc.
    QcState(std::vector<QC::QCRecord>& records, SaveFn save, bool autoAdvance = true);

    mriv::term::KeyResult handleKey(char key);

    size_t index() const { return index_; }
    size_t size() const { return records_.size(); }
    bool empty() const { return records_.empty(); }

    /// The current record. Only valid when !empty().
    const QC::QCRecord& current() const { return records_[index_]; }

    /// True while a verdict change has not been saved successfully.
    bool dirty() const { return dirty_; }

    /// A one-key-press notice ("Saved", "Save failed", ...), shown in place
    /// of the path. Cleared by the next key.
    const std::string& message() const { return message_; }
    void setMessage(std::string message) { message_ = std::move(message); }

    struct Counts
    {
        size_t pass = 0;
        size_t warning = 0;
        size_t fail = 0;
        size_t unrated = 0;
    };
    Counts counts() const;

private:
    mriv::term::KeyResult setVerdict(const char* status);
    mriv::term::KeyResult clearVerdict();
    mriv::term::KeyResult moveTo(size_t index);
    mriv::term::KeyResult jumpUnrated(int direction);
    bool save();

    std::vector<QC::QCRecord>& records_;
    SaveFn save_;
    bool autoAdvance_;
    size_t index_ = 0;
    bool dirty_ = false;
    std::string message_;
};

} // namespace mriv::qc
