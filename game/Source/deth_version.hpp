#pragma once

/**
 * DETH release version (the engine version is PROJECT_VERSION).
 *
 * Naming, so in-game updates keep working for everyone:
 *   stable releases  M.m.p    (1.0.0, 1.0.1)
 *   betas            M.m.b.N  (1.0.b.2: beta 2 after 1.0.0, before the next stable)
 * The release name must equal this string; the release workflows refuse anything else.
 * test/deth_update_test.cpp checks that installed updaters see every new release.
 */
#define DETH_VERSION "1.1.5"
