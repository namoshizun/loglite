## General coding style

- Prefer iteration and modularization over code duplication. Implementation must be elegant, intuitive and Pythonic.
- Follow the "let it crash" principle: avoid excessive error handling and edge-case checks, especially for exploratory development. Do not obscure the main intent with defensive boilerplate.
- When asked to review the code, GO BY THE BOOK! Be thoughtful, critical and brutally honest.
- Don't assume. Don't hide confusion. Surface tradeoffs.
- Your code is for human to read and maintain. Keep readability and maintainability in mind.
  - DO NOT SHOTGUN SLOPS OF LITTLE FUNCTIONS.
  - Variables should be declared as close to their usage as possible.
  - Use blank lines to separate different concepts in your code
- **Important**:
  1. Fix problems at their root cause, not their symptoms.
  2. If a bug reveals a deeper design flaw or incomplete design, propose fixing the design instead.


## Python dev

- All method parameters **must** be typed, all variables **should** be typed wherever sensible.
- Adopt Python 3.10+ typing styles. Must use native collection types (e.g., list, dict) instead of importing them from the typing module (e.g., from typing import List).
- Use loguru instead of the builtin logging module
- Write all Python tests as `pytest` style functions, not `unittest` classes.

## C++ dev

- Write elegant, professional and most importantly, MODERN C++ code. 
- Use C++ 20. Prefer modern features over legacy ones.
- Supports GCC 12+, Clang 16+.
- Follow the Google C++ Style Guide.
- Use CMake and GTest
- Use `cpp/build.sh` to build the debug / release binary.
- Use `cpp/run-tests.sh` to execute unit tests.


## Frontend dev

- Sources: `frontend/`. Dev: `npm run dev` (backend on **7788**). Check: `npm run build`, `npm run lint`. Format: `npm run format` (Prettier; config in `frontend/.prettierrc.json`). UI copy: `src/i18n/` (`en` default, `zh`); use `useI18n().t(...)`.
- Stack: React 19, TypeScript, Vite, Tailwind v4, TanStack Query, Chart.js (`react-chartjs-2`), lucide-react. Dashboard tabs sync via `?tab=` (`useDashboardTab`).
- HTTP/SSE and query encoding: `src/api/client.ts` only; do not duplicate in components. Backend URL: `VITE_API_BASE_URL` in `frontend/.env` (dev, default `http://localhost:7788`) and `frontend/.env.production` (empty = same-origin). Vite dev proxy reads the same variable via `vite.config.ts`.
- Production UI: build `dockerfiles/Dockerfile.frontend` (nginx serves `dist/`, proxies API to `BACKEND_URL`).
- Use `frontend/analyze-bundle.sh` to analyze the bundle size.