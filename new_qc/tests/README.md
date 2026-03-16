# new_qc Test Suite

## Overview

The `new_qc` project includes a comprehensive test suite for CSV loading and saving functionality.

## Test Coverage

### Unit Tests (42 tests total)

All tests verify CSV handler functionality:

1. **CSV Field Escaping** (2 tests)
   - Comma preservation in fields
   - QC status preservation

2. **Load Input CSV** (9 tests)
   - File loading success
   - Record count verification
   - Field-by-field validation
   - Empty initial QC status
   - Empty initial notes

3. **Load Output CSV (Resume)** (8 tests)
   - Existing file loading
   - QC status restoration
   - Notes restoration
   - Multiple records handling

4. **Save Output CSV** (8 tests)
   - File creation
   - Header format
   - Record data preservation
   - QC status saving
   - Notes saving

5. **Special Characters** (6 tests)
   - Commas in quoted fields
   - Escaped quotes handling
   - Multi-record special character files

6. **Empty File Handling** (2 tests)
   - Non-existent file detection
   - Empty file rejection

7. **Resume From Output** (7 tests)
   - Partial QC work loading
   - Status modification
   - Re-save and verification

## Running Tests

### Direct Execution
```bash
cd new_qc/tests/build
./csv_test
```

### CMake Integration
```bash
cd new_qc/tests/build
ctest --output-on-failure
```

## Test Files

- `csv_test.cpp` - Test suite with 42 individual tests
- `CMakeLists.txt` - CMake configuration for test build
- Temporary test files created during testing (auto-cleaned)

## Test Results

```
========================================
Test Summary:
  Total:  42
  Passed: 42
  Failed: 0
========================================

[PASS] All tests passed!
```

## Integration

Tests are integrated with CMake's testing framework:
- Test label: `unit`
- Timeout: 30 seconds
- Working directory: test source directory

## Future Enhancements

Potential additions:
- Image loading tests (stb_image integration)
- UI interaction tests (requires display)
- Performance benchmarks for large CSV files
- Edge case testing (very long fields, Unicode, etc.)
