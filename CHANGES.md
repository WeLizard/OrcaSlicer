# FilamentHub Modifications to OrcaSlicer

This fork of OrcaSlicer includes modifications to integrate FilamentHub functionality and fix Windows build issues.

**License:** AGPL-3.0 (same as original OrcaSlicer)  
**Source:** https://github.com/lizardjazz1/OrcaSlicer  
**Branch:** filamenthub-integration  
**Original:** https://github.com/SoftFever/OrcaSlicer

---

## Build Fixes (2025-01-XX)

### 1. OpenCV Detection Fix

**Files Modified:**
- `CMakeLists.txt` (lines ~278-299)
- `src/libslic3r/CMakeLists.txt` (lines ~492-582)

**Changes:**
- Added explicit `OpenCV_RUNTIME` and `OpenCV_ARCH` detection for Windows/MSVC builds
- OpenCVConfig.cmake may not correctly detect MSVC_VERSION on some systems
- Added fallback to manual OpenCV_DIR path resolution
- Added direct library search if `find_package` fails

**Reason:** Windows builds were failing due to OpenCV detection issues.

### 2. OCCT DLL Path Resolution Fix

**File Modified:**
- `CMakeLists.txt` (function `orcaslicer_copy_dlls`, lines ~831-883)

**Changes:**
- Added search for OCCT DLLs in multiple possible paths:
  - `bin/occt/`
  - `win64/vc17/bin/`
  - `win64/vc16/bin/`
  - `win64/vc15/bin/`
  - `win64/vc14/bin/`
- Only copy DLLs if they exist (prevent build errors)

**Reason:** OCCT DLLs location varies depending on build configuration.

---

## Planned Modifications (Future)

### FilamentHub Integration

The following modifications are planned but not yet implemented:

1. **FilamentHub Authentication**
   - New file: `src/slic3r/GUI/FilamentHubAuth.cpp/.h`
   - Authentication with FilamentHub API (similar to BambuLab integration)

2. **FilamentHub Tab**
   - New file: `src/slic3r/GUI/FilamentHubPanel.cpp/.h`
   - New tab "FilamentHub" in main UI (next to Prepare, Preview, Printer, Project tabs)

3. **Profile Synchronization**
   - Modify: `src/slic3r/GUI/FilamentProfileDialog.cpp` (or similar)
   - Add FilamentHub profiles to "Filament Profile" dropdown
   - Auto-sync user profiles from FilamentHub

---

## Copyright Notices

### Original OrcaSlicer
Copyright (C) SoftFever/OrcaSlicer contributors  
Licensed under AGPL-3.0

### FilamentHub Modifications
Copyright (C) 2025 FilamentHub  
Licensed under AGPL-3.0

---

## License Compliance

All modifications comply with AGPL-3.0 license requirements:
- ✅ Fork is public on GitHub
- ✅ Original copyrights preserved
- ✅ LICENSE.txt unchanged
- ✅ All modifications are licensed under AGPL-3.0
- ✅ Source code available to all users

---

## Contact

For questions about FilamentHub modifications:
- **Repository:** https://github.com/lizardjazz1/OrcaSlicer
- **Original Project:** https://github.com/SoftFever/OrcaSlicer
- **FilamentHub Project:** https://github.com/lizardjazz1/FilamentHub

---

**Last Updated:** 2025-01-XX

