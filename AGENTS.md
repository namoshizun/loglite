## General coding style

- Prefer iteration and modularization over code duplication.
- Follow the "let it crash" principle: avoid excessive error handling and edge case checks, especially when implementing experimental solutions or features. Don't let the main intent of functions and classes be obscured by boilerplate exception handling.
- **Important**: try to fix things at the cause, not the symptom.
- When asked to review the code, GO BY THE BOOK! As if you are a reviewer in bad mood.


## Python dev

- Your implementation must be elegant, intuitive and Pythonic.
- All method parameters **must** be typed, all variables **should** be typed wherever sensible.
- Adopt Python 3.10+ typing styles. Must use native collection types (e.g., list, dict) instead of importing them from the typing module (e.g., from typing import List).
- Use loguru instead of the builtin logging module
- Write all Python tests as `pytest` style functions, not `unittest` classes.

## C++ dev

- Write elegant, professional and most importantly, MODERN C++ code. 
- Use C++ 20. Prefer modern features over legacy ones.
- Follow the Google C++ Style Guide.
- Use CMake and GTest
- Use `cpp/build.sh` to build the debug / release binary.
- Use `cpp/run-tests.sh` to execute unit tests.


## Frontend dev

- Sources: `frontend/`. Dev: `npm run dev` (backend on **7788**). Check: `npm run build`, `npm run lint`. Format: `npm run format` (Prettier; config in `frontend/.prettierrc.json`). UI copy: `src/i18n/` (`en` default, `zh`); use `useI18n().t(...)`.
- Stack: React 19, TypeScript, Vite, Tailwind v4, TanStack Query, Chart.js (`react-chartjs-2`), lucide-react. Dashboard tabs sync via `?tab=` (`useDashboardTab`).
- HTTP/SSE and query encoding: `src/api/client.ts` only; do not duplicate in components.
- Live SSE in dev uses direct `localhost:7788` (`getSSEUrl`) — do not route SSE through the Vite proxy.
- Use `frontend/analyze-bundle.sh` to analyze the bundle size.