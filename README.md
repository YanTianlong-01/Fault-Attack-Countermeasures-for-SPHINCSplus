# SPHINCS+ Fault Attack Artifact

## Overview

This artifact reproduces the fault injection experiments described in the paper:
> *"Fault Attack Countermeasures for SPHINCS+: WOTS+ with Checksum Segmentation and Subtree Root Children Caching"*
> Yan Li, **Tianlong Yan**, et al.
> IACR Transactions on Cryptographic Hardware and Embedded Systems (TCHES), 2026.

The experiments demonstrate **Phase 1** (fault injection) of the fault attack against SPHINCS+, conducted on the **sphincs-shake-256s-robust** parameter set using a **ChipWhisperer** setup with an STM32F3 target.

---

## ⚠️ Special Tools Required

Beyond a standard desktop/laptop Unix computing environment, this artifact requires:

| Category | Item | Notes |
|----------|------|-------|
| **Hardware** | ChipWhisperer Lite (CW1170) or Pro (CW1200) | The fault injection measurement tool |
| **Hardware** | NAE-CW308T-STM32F3 target board | DUT running SPHINCS+ firmware |
| **Hardware** | External power supply for target (recommended) | Some USB ports cannot supply sufficient current |
| **Software** | ChipWhisperer framework ≥ 5.6 | `pip install chipwhisperer==5.6.1` |
| **Software** | ARM cross-compiler (`arm-none-eabi-gcc`) | For compiling the STM32 firmware |
| **OS** | Linux (Ubuntu 22.04 LTS recommended) | Firmware compilation and ChipWhisperer tools work best on Linux |
| **OS** | Python ≥ 3.9 | For running experiment scripts |

> **Note on OS:** While the Python tools (`SPHINCSplus.py`, `cwsetup.py`, `cwfaultexp.py`) are cross-platform, firmware compilation and ChipWhisperer operation are officially supported on **Linux**. The provided experiment logs in `logs/` were collected on Ubuntu 22.04.5 LTS.

---

## Repository Structure

```
SPHINCSplus Fault Attack Code/
├── SPHINCSplus.py          # Pure-Python SPHINCS+ implementation (SHAKE256)
│                           #   Used to verify signatures and reproduce fault
│                           #   injection effects offline.
├── tools/
│   ├── cwsetup.py          # ChipWhisperer connection & firmware flashing utility
│   ├── cwfaultexp.py       # Main experiment runner (fault injection campaign)
│   └── requirements.txt     # Python dependencies for tools/
├── hardware/
│   └── victims/
│       └── firmware/
│           ├── simpleserial-sphincsplus/
│           │   ├── simpleserial-sphincsplus.c  # Main firmware (SimpleSerial protocol)
│           │   └── makefile                     # Firmware build file
│           └── crypto/
│               ├── SPHINCSplus/                # SPHINCS+ reference C implementation
│               │   ├── sign.c                   # Adapted for single-layer signing
│               │   ├── wots.c / wotsx1.c
│               │   ├── params/                  # Parameter sets (SHAKE/SHA256/Haraka)
│               │   └── ...
│               ├── Makefile.crypto              # Crypto lib build
│               └── Makefile.sphincsplus         # SPHINCS+ build config
└── logs/
    ├── 2025-06-20_09-09-01_SPHINCSplus.txt   # Raw experiment log (5 trials)
    ├── analysis.ipynb                        # Jupyter notebook: parse & analyze faulty signatures
    └── split_log.ipynb                      # Jupyter notebook: split log by trial
```

---

## Step-by-Step Guide

### Step 1 — Set Up the Physical Experiment Platform

#### 1.1 Hardware Assembly

1. Connect the **ChipWhisperer Lite/Pro** to your PC via USB.
2. Connect the **CW308T-STM32F3** target board to the ChipWhisperer's 20-pin HS2 connector.
3. (Recommended) Power the target board via an **external 3.3V DC supply** rather than relying on USB power, to ensure stable clock/glitch operation.
4. Verify the setup by running:
   ```bash
   python3 -c "import chipwhisperer as cw; print(cw.__version__)"
   ```

#### 1.2 Install ARM Cross-Compiler (if not already installed)

```bash
# Ubuntu / Debian
sudo apt update
sudo apt install gcc-arm-none-eabi libnewlib-arm-none-eabi

# Verify
arm-none-eabi-gcc --version
```

#### 1.3 Clone the ChipWhisperer SDK

```bash
git clone https://github.com/newaetech/chipwhisperer.git
cd chipwhisperer
git checkout 5.6.1        # Use the version tested with this artifact
pip install -e .
```

> **Important:** This artifact was tested with `chipwhisperer==5.6.1`. Other versions may work but are not guaranteed.

---

### Step 2 — Compile the Firmware

The firmware implements a **single-layer** version of SPHINCS+ signing, targeting a specific XMSS subtree layer to speed up the signing process (full SPHINCS+ signing on STM32F3 takes ~28 seconds per signature, too long for practical fault injection).

#### 2.1 Integrate the SPHINCS+ sources into the ChipWhisperer SDK

The firmware reuses the ChipWhisperer build system (`Makefile.inc`, `simpleserial/`, and the per-platform `hal/`), so it must be compiled **inside** the SDK's firmware directory. Two of our files are also patches to that build system:

- `crypto/Makefile.crypto` — adds the `SPHINCSplus` branch to `$(CRYPTO_TARGET)`.
- `crypto/Makefile.sphincsplus` — the build rules for the SPHINCS+ sources.

> **Important — directory layout.** In ChipWhisperer **5.6.1** the firmware root is `firmware/mcu/` (it contains `Makefile.inc`, `simpleserial/`, `hal/`, `crypto/`, ...). If you are on a newer/different SDK checkout where the layout is `hardware/victims/firmware/`, use that path instead — the steps below are otherwise identical. You can confirm the correct root by checking that it contains a `Makefile.inc` file.

Copy our sources **on top of** the SDK's firmware directory (the `/.` makes `cp` overwrite existing files like `Makefile.crypto`):

```bash
# Set this to the SDK firmware directory you confirmed above:
FW_DIR=/path/to/chipwhisperer/firmware/mcu      # CW 5.6.1
# FW_DIR=/path/to/chipwhisperer/hardware/victims/firmware   # some newer checkouts

# From the artifact root:
cp -r hardware/victims/firmware/crypto/.            "$FW_DIR/crypto/"
cp -r hardware/victims/firmware/simpleserial-sphincsplus "$FW_DIR/"

# Sanity check: our patched Makefile.crypto must now contain the SPHINCSplus branch
grep -c SPHINCSplus "$FW_DIR/crypto/Makefile.crypto"   # should print: 3
```

> **Do not** run `make -f Makefile.sphincsplus` on its own — that file only defines variables and has no build targets, so it will fail with `make: *** No targets.  Stop.` It is automatically pulled in by `Makefile.crypto` when you pass `CRYPTO_TARGET=SPHINCSplus` in the next step.

#### 2.2 Build the firmware

Build from inside the `simpleserial-sphincsplus` directory. The crypto library is compiled automatically as part of this step:

```bash
cd "$FW_DIR/simpleserial-sphincsplus"

# Compile for the STM32F3 target (as used to collect the provided logs)
make PLATFORM=CW308_STM32F3 CRYPTO_TARGET=SPHINCSplus
```

This produces `simpleserial-sphincsplus-CW308_STM32F3.hex` (plus `.elf`/`.bin`/`.map`).

> **Platform note.** The paper used `CW308_STM32F4` (STM32F4 target); the provided firmware code is also compatible with `CW308_STM32F3` (used for log collection). Adjust `PLATFORM` as needed. If you switch to `CW308_STM32F4`, also change `PLATFORM` in `tools/cwsetup.py`.
>
> **Toolchain.** Requires `arm-none-eabi-gcc` (Step 1.2) and the SDK `hal/` directory present — both come from a complete ChipWhisperer SDK checkout, which is why Step 2.1 copies into the SDK tree rather than building standalone.

---

### Step 3 — Flash the Firmware

With the ChipWhisperer connected and target powered:

With the ChipWhisperer connected and target powered, flash the firmware:

```bash
cd tools
python3 cwsetup.py
```

`cwsetup.py` connects to the ChipWhisperer and flashes the firmware produced in Step 2. By default it looks for the `.hex` under the path set by the `CW_FW_FOLDER` environment variable (the `simpleserial-sphincsplus` directory that contains the built `.hex`). Set it to your build directory before running, for example:

```bash
export CW_FW_FOLDER=/path/to/chipwhisperer/firmware/mcu/simpleserial-sphincsplus
python3 cwsetup.py
```

If `CW_FW_FOLDER` is unset, the script will prompt you for the path interactively. After flashing it verifies connectivity by reading back `"SPHINCS+\n"` from the target.

> The experiment runner `cwfaultexp.py` reads the same `CW_FW_FOLDER` variable (see Step 5).

---

### Step 4 — Install Python Dependencies

A single `requirements.txt` is provided at the repository root and covers both the experiment scripts and the analysis notebooks:

```bash
pip install -r requirements.txt
```

Key packages:
- `chipwhisperer==5.6.1` (already installed in Step 1)
- `pycryptodome` — for `Crypto.Hash.SHAKE256`, used by `SPHINCSplus.py`
- `numpy`, `sympy` — used by the analysis notebooks / offline verification
- `jupyter` — to run the notebooks in Step 6

> **Python version.** The `numpy` lower bound is set for Python 3.12 compatibility. The artifact has been tested on Python 3.10 and 3.12.

---

### Step 5 — Run the Fault Injection Experiment

The main experiment script is `tools/cwfaultexp.py`. It performs the following:

- **Experiment 1 (`run_exp1`):** Straight signing — injects clock glitches during the signing of a specific XMSS layer to produce faulty signatures and/or faulty subtree roots. This reproduces the **Phase 1** attack.


The paper's experiment logs were produced by `run_exp1` with the following parameters:
- **Target:** sphincs-shake-256s-robust
- **Layer targeted:** Layer `EXP_STR_LAYER = 5` (0-indexed, i.e., the 6th layer from the bottom)
- **Glitch parameters:** `ext_offset=37`, `offset=5`, `width=7`, `output=glitch_only`, `trigger_src=manual`
- **Trials:** 5 independent experiments, 1,024 signatures each

To reproduce the experiment:

```bash
cd tools
python3 cwfaultexp.py
```

The script will:
1. Connect to the ChipWhisperer and reset the target.
2. For each of `N=5` trials, send 1,024 signing commands (`'x'` API), injecting a clock glitch after a delay in each iteration.
3. Log all results (correct signatures, faulty signatures, faulty roots) to `../logs/<timestamp>_SPHINCSplus.txt`.

#### Adjusting the Experiment

Key parameters at the top of `cwfaultexp.py`:

```python
N = 5          # Number of independent experiments
M = 1024       # Signatures per experiment
DURATION = 28  # Approximate signing duration in seconds (STM32F3 single layer)
```

Or, for a quick dry-run with fewer signatures:

```python
N = 1
M = 50
```

#### Glitch Parameter Exploration

The `run_exp_expl()` function systematically sweeps `ext_offset`, `offset`, and `width` to find glitch parameters that maximize faulty signatures without causing target resets. Use this if you are targeting a different platform or clock frequency:

```python
# In cwfaultexp.py, uncomment:
# run_exp_expl(logged=True)
```

---

### Step 6 — Analyze the Results

This step **does not require the ChipWhisperer hardware** — it runs purely on the provided log files, so it can be reproduced on any machine (see "Reproducing Table 6" below).

Install Jupyter first if you do not have it (it is also listed in `requirements.txt`):

```bash
pip install jupyter
```

Two Jupyter notebooks are provided in `logs/`:

#### 6.1 Reproduce Table 6 (`analysis.ipynb`)

This is the main notebook. Open it from the `logs/` directory:

```bash
cd logs
jupyter notebook analysis.ipynb   # or: jupyter lab analysis.ipynb
```

The notebook loads the **combined** raw log (`2025-06-20_09-09-01_SPHINCSplus.txt`) and **automatically splits it by experiment** (on the `Launching experiment` markers), so you do not need any pre-processing. For each of the 5 experiments it:

1. Parses the log to extract `(signature, root)` pairs per iteration (matching the `Received sig ...` / `Received root ...` lines).
2. Takes the first glitch-free iteration of each experiment as the reference.
3. Counts **correct**, **faulty-signature (root correct)**, and **faulty-root** outcomes, and computes the Phase-1 fault-injection success rate.

To analyze your own log instead, set the `input_file` variable at the top of the notebook to your `<timestamp>_SPHINCSplus.txt`.

> **If you only want to reproduce the paper's numbers**, just run every cell of `analysis.ipynb` as-is — it ships pointed at the provided log.

#### 6.2 Split a Log by Trial (`split_log.ipynb`)

Optional helper. If you ran `N > 1` trials and want the individual per-experiment files written to disk (`*_exp1.txt`, `*_exp2.txt`, ...), run `split_log.ipynb`. This is not required for `analysis.ipynb`, which already handles the combined log internally.

---

## Interpreting the Results

The expected outcomes (as reported in the paper, Table 6) are:

| Outcome | Count (per 1,024 signatures) | Probability |
|---------|-------------------------------|-------------|
| Correct signature | ~850 | ~83% |
| Faulty signature (root correct) | ~10 | ~1% |
| **Faulty root** | ~160 | **~16%** |

The **"Faulty root"** column corresponds to successful Phase 1 attacks — a fault was injected during the Merkle tree computation, corrupting the subtree root. This is the necessary condition for the WOTS+ key reuse attack described in Phase 1 of the paper's Algorithm 1.

With the **SRCC countermeasure** enabled (simulated by targeting a lower layer via the `inplength` parameter in `run_exp1`), the effective fault injection success rate drops because the top cached layers are no longer vulnerable.

---

## Firmware API Reference

The flashed firmware exposes the following SimpleSerial commands:

| Command | Input | Description |
|---------|-------|-------------|
| `'x'` | 8 bytes (tree address) | Sign at `EXP_STR_LAYER`, trigger then read WOTS+ sig + XMSS auth path |
| `'u'` | 2 bytes (index) | Read the computed subtree root |
| `'r'` | 2 bytes (index) | Read the *i*-th 32-byte block of the cached WOTS+ signature |
| `'k'` | 96 bytes | Set SPHINCS+ keys (sk_seed, sk_prf, pk_seed) |
| `'z'` | 8 bytes (tree address) | Cached signing (SRCC simulation) |
| `'q'` | 8 bytes (address) | Fill cache for cached signing |

---

## Reproducing the Paper's Table 6

To reproduce Table 6 from the paper, run `analysis.ipynb` on the provided log file:

```
logs/2025-06-20_09-09-01_SPHINCSplus.txt
```

The notebook will compute:

```
Exp 1: 844 correct, 180 faulty sig, 172 faulty root, 8 faulty sig (root correct)
Exp 2: 860 correct, 164 faulty sig, 156 faulty root, 8 faulty sig (root correct)
Exp 3: 839 correct, 185 faulty sig, 176 faulty root, 9 faulty sig (root correct)
Exp 4: 849 correct, 175 faulty sig, 164 faulty root, 11 faulty sig (root correct)
Exp 5: 850 correct, 174 faulty sig, 162 faulty root, 12 faulty sig (root correct)
```

Average fault injection (faulty root) rate: **~16.2%** — matching the paper's experimental results.

---

## Offline Verification with `SPHINCSplus.py`

The `SPHINCSplus.py` file is a **pure-Python SPHINCS+ implementation** (SHAKE256 only) that can be used to:

1. **Verify signatures** from the raw log files:
   ```python
   from SPHINCSplus import SPHINCSplus, SPHINCSPLUS_INSTANCES
   spx = SPHINCSplus("256s")
   spx.keygen(sk_seed, sk_prf, pk_seed, pk_root)
   # Parse and verify signatures from the log
   ```

2. **Simulate fault effects** using the `fault_sign()` method to reproduce the WOTS+ reuse scenario offline:
   ```python
   # Sign same leaf with two different messages to simulate key reuse
   sig_A = spx.fault_sign(msg_A, layer=5, verifying=False)
   sig_B = spx.sign(...)  # same WOTS+ address
   # Now both sigs can be analyzed with the GF-based analysis
   ```

3. **Reproduce the WOTS+ post-reuse security analysis** from the paper's Section 3.2 using the `WOTSplus` class directly.

---

## Troubleshooting

### Target does not respond / "TimeoutError"

- Ensure the target board is powered (try an external 3.3V supply).
- Check that the ChipWhisperer firmware is up to date: run `python3 -m chipwhisperer.capture.utils.platupdate` in the ChipWhisperer repository.
- Try pressing the reset button on the CW308 board before flashing.

### No faulty signatures produced

- The glitch parameters (`ext_offset`, `offset`, `width`) are highly platform-dependent. Start with `run_exp_expl()` to find parameters that produce a mix of correct and faulty outputs.
- For STM32F3, the parameters used in the paper were: `ext_offset=37`, `offset=5`, `width=7`.
- For STM32F4, you may need to re-explore these values.

### `chipwhisperer` import fails

```bash
pip install chipwhisperer==5.6.1
# Or from source:
cd /path/to/chipwhisperer
pip install -e .
```

### Compilation errors on firmware

- Ensure `arm-none-eabi-gcc` is in your PATH.
- The firmware must be built **inside** the ChipWhisperer SDK firmware tree (which provides `Makefile.inc`, `simpleserial/`, and the `hal/` directory) — see Step 2. Building standalone will fail with missing `Makefile.hal` or `Unknown CRYPTO_TARGET`.
- `Unknown or blank CRYPTO_TARGET: SPHINCSplus` means the SDK is using its **original** `Makefile.crypto` instead of the patched one — re-run the `cp -r ... crypto/.` command from Step 2.1 (the `/.` overwrites `Makefile.crypto`), then confirm with `grep SPHINCSplus Makefile.crypto`.

---

## License

This artifact is released under the **MIT License** (see [`LICENSE`](LICENSE)). The SPHINCS+ reference C implementation, the ChipWhisperer framework, and the upstream SPHINCSplus-FA code retain their original copyright and license terms; see the `LICENSE` file for details.

---

## Acknowledgments

The attack implementation in this artifact is based on the open-source code from [https://github.com/AymericGenet/SPHINCSplus-FA](https://github.com/AymericGenet/SPHINCSplus-FA). We thank the authors for making their code publicly available.

