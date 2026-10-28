# EOS M port verification

Run from the Soup Engine repository:

```sh
make -C src test
python3 src/ml/eosm/tests/run.py /absolute/path/to/EOS-M-photo.CR2
```

The adapter suite compiles the real `soup.c` worker and `develop.c` pipeline
against mocked Magic Lantern I/O, under AddressSanitizer and UBSan. UBSan errors
are fatal. It renders every indexed stock at half and full preview size and
compares the complete JPEG bytes with the upstream desktop `previewcheck`
renderer. The CR2 is read-only, with a SHA-256 check before and after.

It also exercises partial reads, initial and mid-stream read/write failures,
failed DMA allocation, cancellation during rendering, guarded output creation,
missing inputs, invalid scale, movie/shutdown/disabled cancellation flags,
existing zero-byte outputs, Adobe RGB filenames, and a burst crossing DCIM
folders. Read and write calls must use the DMA buffer's original base, including
after odd-length partial reads. Every job must release its file handles and DMA
allocations. ASan covers the normal allocations as well.

Results on 2026-10-27:

- ARM module build passed with warnings as errors; official EOSM.202 core and
  its 19 selected standard modules built successfully.
- All Soup module imports resolved against the matching `EOSM_202.sym`.
- 13 upstream engine self-checks passed.
- 26 adapter/reference render comparisons passed, byte-for-byte, with no
  sanitizer errors; source CR2 unchanged.
- The final archive passed its ZIP integrity check, all 70 staged-file hashes,
  the expected 20 modules, 13 film stocks and essential fonts.

The sample was raw.pixls.us entry 1224, original EOS M RAW (3:2), CC0:
[download](https://raw.pixls.us/getfile.php/1224/nice/Canon%20-%20EOS%20M%20-%20RAW%20(3%3A2).CR2).
SHA-256: `27d151efe86f04bea8fcf6ec9f3a722ae3ee24a3f56a6ebbd27ee89b215cf569`.
The embedded JPEG is 5184 × 3456, yielding 2592 × 1728 at half size.

A later I/O fix uses ML's supported file-create API and synchronous read mode,
and adds operation-specific status messages. The quick adapter comparison and
injected I/O tests passed again after that fix.

On 2026-10-27, a user confirmed boot, module loading and film JPEG generation on
an original EOS M after the I/O fix, reporting about 45 seconds per photo. Output
size for that timing was not confirmed. Capture coexistence, on-camera playback,
long-term stability and orderly shutdown still need broader validation.
See [EOSM.md](../../../../EOSM.md).
