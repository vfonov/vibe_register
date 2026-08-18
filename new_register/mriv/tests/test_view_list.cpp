/// test_view_list.cpp — parsing the --views argument into the parent's
/// viewIndex convention.
///
/// The list is what decides how many rows the display grid has, so an
/// accepted-but-wrong list produces a plausible-looking layout showing the
/// wrong planes. Order is the user's, not canonical: "y,z" means coronal
/// above axial.

#include <cassert>

#include "cli/ViewList.hpp"

using namespace mriv::term;

namespace
{

void testDefaultListIsAllThreePlanes()
{
    auto views = parseViewList("z,x,y");
    assert(views.has_value());
    assert(views->size() == 3);
    assert((*views)[0] == 0); // axial
    assert((*views)[1] == 1); // sagittal
    assert((*views)[2] == 2); // coronal
}

void testSingleView()
{
    auto views = parseViewList("z");
    assert(views.has_value());
    assert(views->size() == 1);
    assert((*views)[0] == 0);
}

/// The order given is the order shown: this is a layout instruction, not a
/// set, so it must not be sorted into a canonical order behind the user's
/// back.
void testOrderIsPreserved()
{
    auto views = parseViewList("y,z");
    assert(views.has_value());
    assert(views->size() == 2);
    assert((*views)[0] == 2); // coronal first
    assert((*views)[1] == 0); // axial second
}

/// A repeat would render the same plane twice and waste a row, which is
/// never what was meant. Drop it and keep the first occurrence's position.
void testDuplicatesAreDropped()
{
    auto views = parseViewList("z,x,z");
    assert(views.has_value());
    assert(views->size() == 2);
    assert((*views)[0] == 0);
    assert((*views)[1] == 1);
}

void testWhitespaceIsTolerated()
{
    auto views = parseViewList(" z , x ");
    assert(views.has_value());
    assert(views->size() == 2);
    assert((*views)[0] == 0);
    assert((*views)[1] == 1);
}

void testInvalidListsRejected()
{
    assert(!parseViewList("").has_value());
    assert(!parseViewList("w").has_value());
    assert(!parseViewList("z,w").has_value());
    assert(!parseViewList("axial").has_value());
    assert(!parseViewList(",").has_value());
    assert(!parseViewList("z,,x").has_value());
}

} // namespace

int main()
{
    testDefaultListIsAllThreePlanes();
    testSingleView();
    testOrderIsPreserved();
    testDuplicatesAreDropped();
    testWhitespaceIsTolerated();
    testInvalidListsRejected();
    return 0;
}
