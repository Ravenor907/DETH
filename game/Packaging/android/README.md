# Android signing

`deth-release.keystore` signs every DETH APK built by CI (alias `deth`, password `deth-release`).
Android only installs an update over an existing app when both are signed with the same key, so
the key has to stay the same from release to release. Keeping it in the repository is a deliberate
trade-off for a sideloaded hobby app: anyone can build an APK that installs over DETH, but only if
you choose to install it. If you want a private key instead, set the repository secrets
`DETH_KEYSTORE_BASE64`, `DETH_KEYSTORE_PASSWORD`, `DETH_KEY_ALIAS` and `DETH_KEY_PASSWORD`; they take
precedence. Note that switching keys later makes the next update a fresh install.
