# Fake Central EGA fixtures

This directory contains development-only fixtures for emulating Central EGA.
They are not part of the production Compose deployment documented in
`deploy/docker`.

The fixture credentials and data must never be used in a production
environment. A separate, explicit test harness is required to run these
components.
