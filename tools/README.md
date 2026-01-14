# Tools

This folder contains helper scripts for development and diagnostics.

## send-petscii.py

Inject a PETSCII byte range using the same mechanism as the plugin:

- Writes bytes to the KERNAL keyboard buffer at `$0277..$0280`.
- Sets the buffer length at `$00C6`.
- Uses backpressure by polling `$00C6` until the C64 consumes the data.

Examples:

```bash
# Always hex: $60..$7F
tools/send-petscii.py 60-80

# Single byte
tools/send-petscii.py 6E
```

If your REST endpoint requires a password:

```bash
C64U_PASSWORD=yourpass tools/send-petscii.py 60-80
```

Notes:

- Base URL is fixed to `http://c64u`.
- The range is half-open (end is exclusive).
