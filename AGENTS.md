## General coding style

- Prefer iteration and modularization over code duplication.
- Use descriptive variable names with auxiliary verbs (e.g., is_active, has_permission).
- Follow the "let it crash" principle: avoid excessive error handling and edge case checks, especially when implementing experimental solutions or features. Don't let the main intent of functions and classes be obscured by boilerplate exception handling.
- **Important**: try to fix things at the cause, not the symptom.


## Python coding guide

- Your implementation must be elegant, intuitive and Pythonic.
- All method parameters **must** be typed, all variables **should** be typed wherever sensible.
- Adopt Python 3.10+ typing styles. Must use native collection types (e.g., list, dict) instead of importing them from the typing module (e.g., from typing import List).
- Use loguru instead of the builtin logging module
- Write all Python tests as `pytest` style functions, not `unittest` classes.

## C++ coding style

- Write elegant, professional and most importantly, MODERN C++ code. 
- Use C++ 23. Prefer modern features over legacy ones.
- Use CMake
- Use GTest