# FilamentHub Modifications to OrcaSlicer

This fork of OrcaSlicer includes modifications to integrate FilamentHub functionality and fix Windows build issues.

**License:** AGPL-3.0 (same as original OrcaSlicer)
**Source:** https://github.com/WeLizard/OrcaSlicer
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

## FilamentHub Integration (2025-01-XX)

### 1. FilamentHub HTTP Client

**Files Created:**
- `src/slic3r/Utils/FilamentHubClient.cpp/.hpp`

**Changes:**
- HTTP client for FilamentHub REST API
- Implements `test_connection()`, `login()`, `get_current_user()` methods
- Uses existing `Http` class (libcurl-based) from OrcaSlicer
- Default API URL: `http://localhost:8000` (configurable)
- JWT token management (get/set/clear access_token)

### 2. FilamentHub Tab

**Files Created:**
- `src/slic3r/GUI/FilamentHubPanel.cpp/.hpp`

**Files Modified:**
- `src/slic3r/GUI/MainFrame.hpp` - Added `tpFilamentHub` to `TabPosition` enum, added `m_filamenthub_panel` member
- `src/slic3r/GUI/MainFrame.cpp` - Added FilamentHubPanel tab creation in `init_tabpanel()`

**Changes:**
- New tab "FilamentHub" in main UI (next to Calibration tab)
- UI skeleton with:
  - Status label (Connected/Not connected)
  - Test Connection button
  - Login/Logout buttons
  - Placeholder content area
- Integrated with FilamentHubClient for API communication

### Planned Modifications (Future)

3. **FilamentHub Authentication Dialog**
   - Login dialog with email/username and password fields
   - Store JWT token locally
   - Auto-refresh token mechanism

4. **Profile Synchronization**
   - Modify: `src/slic3r/GUI/FilamentProfileDialog.cpp` (or similar)
   - Add FilamentHub profiles to "Filament Profile" dropdown
   - Auto-sync user profiles from FilamentHub
   - Import/export functionality

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
- **Repository:** https://github.com/WeLizard/OrcaSlicer
- **Original Project:** https://github.com/SoftFever/OrcaSlicer
- **FilamentHub Project:** https://github.com/WeLizard/FilamentHub

---

**Last Updated:** 2025-01-XX

