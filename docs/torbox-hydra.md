# TorBox and Hydra catalogs

Open **Settings > Debrid**, paste your TorBox API key, choose **Save and test**, then turn on **Use TorBox**. Your TorBox plan must support API access.

While TorBox is enabled, all torrent downloads use TorBox, including updates, add-ons, restored jobs, and retries. Enabling it switches unfinished peer downloads to TorBox; paused jobs stay paused. Existing peer files are preserved, but TorBox downloads use a separate folder and cannot reuse those files. A missing key or provider error fails the job instead of falling back to peers. Background peer metadata probes stop as well. Direct HTTP downloads continue through their existing downloader.

TorBox fetches the torrent remotely. Sprout waits until its files are ready, then streams each file into the original folder layout. Local transfers support pause and resume, use byte ranges where available, and retain partial files after interruptions. Canceling a local transfer does not remove the torrent from your TorBox account. Jobs already assigned to TorBox retain that method when the setting is turned off.

The key is stored separately from settings and download history. Windows protects it with DPAPI for the current user; Linux uses a file readable and writable only by its owner. It is sent to the TorBox API, never as an authorization header to file servers.

Catalog URLs hosted on `hydralinks.cloud` or `hydralinks.pages.dev` use Hydra's indexed API rather than downloading the protected JSON directly. Catalog loading is paginated; download links are fetched when a game is downloaded. The newest available release from the selected source is used. Sources must be indexed by Hydra, and availability depends on that service. Direct links use Sprout's existing HTTP downloader; hoster pages requiring a dedicated downloader are not supported by this integration.

## Validation

The standalone Qt tests exercise catalog pagination, source filtering, lazy links, cancellation, direct links, TorBox routing on restore and retry, switching active peer jobs, streaming resume with supported and ignored byte ranges, invalid ranges, pause/resume, multiple-file layouts, path traversal, persisted remote IDs, and separate key storage. A live check loaded a protected Hydra catalog and resolved a download link. TorBox API and file responses are fixtures; a real account is still needed for an end-to-end provider check.

```sh
cmake -S tests -B build-network-tests -DCMAKE_PREFIX_PATH=/path/to/Qt
cmake --build build-network-tests
ctest --test-dir build-network-tests --output-on-failure
```

The Linux validation workflow builds the full launcher with Qt 6.11.1 on Ubuntu 24.04 and runs these tests on every push to `main`. Linux tests also check private key-file permissions and reject download paths through symlinks. Actual TorBox transfers and game installation still need testing on your Linux machine.
