# SPDX-License-Identifier: MIT

Thank you for considering contributing to mod_audio_stream!

How to contribute
- Bug reports: Open an issue with clear steps to reproduce, logs, and environment info.
- Feature requests: Open an issue describing the use case and proposed API changes/events.
- Pull requests: Fork and submit a PR. Keep changes small and focused.

Development setup
- Install FreeSWITCH dev headers, libwebsockets, and speexdsp.
- Build with CMake as described in README.md.

Coding guidelines
- C/C++: follow existing style; prefer C++11 in .cpp files.
- Add SPDX-License-Identifier: MIT to new files.
- Avoid introducing new third-party code unless necessary; prefer system packages.
- Document new environment variables, events, or API changes in README.

Testing
- Manual testing via fs_cli:
  - uuid_audio_stream <uuid> <streamid> start <wss-url> inbound 16k 60 0 {}
  - Observe logs, events, and remote server traffic.

Security
- Do not weaken TLS checks by default. For development, guards exist to allow self-signed certs via env vars.

License
By contributing, you agree that your contributions are licensed under the MIT License.
