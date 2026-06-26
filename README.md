# Mini Firmware Boot

A collection of **from-scratch, zero-dependency C implementations** of firmware, bootloader, and system bootstrap concepts. Each module simulates or models real firmware behavior — from BIOS/UEFI initialization and bootloader stages to secure boot chains, TPM-based measured boot, and remote attestation. Modules map to MIT, CMU, and industry standards, bridging firmware theory to runnable C code.

## Modules

| Module | Topics | Key References |
|--------|--------|----------------|
| [mini-firmware](mini-firmware/) | Firmware architecture, flash layout, reset vector, ROM/RAM init, memory-mapped I/O | Intel Firmware, ARM Trusted Firmware |
| [mini-boot-process](mini-boot-process/) | Power-on reset, boot phases (SEC/PEI/DXE/BDS), CPU init, memory init, device enumeration | UEFI PI Spec, AMD AGESA |
| [mini-bios-uefi](mini-bios-uefi/) | Legacy BIOS (int 0x19, int 0x13), UEFI (PE/COFF, GPT, protocols), CSM compatibility | Phoenix BIOS, TianoCore EDK II |
| [mini-bootloader](mini-bootloader/) | Stage1/Stage2 bootloader, GRUB, U-Boot, Linux boot protocol, initramfs, multiboot | GRUB2, Das U-Boot |
| [mini-hardware-desc](mini-hardware-desc/) | Device Tree (DTS/DTB), ACPI tables (DSDT/SSDT), SMBIOS, HOB (Hand-Off Blocks) | Linux DTSpec, ACPI Spec 6.5 |
| [mini-secure-boot](mini-secure-boot/) | UEFI Secure Boot, PK/KEK/db/dbx, signed EFI images, root of trust, verified boot chain | UEFI Spec Ch 32 |
| [mini-measured-boot](mini-measured-boot/) | TPM 2.0 PCR banks, measurement log (TPM Event Log), CRTM, SRTM vs DRTM, Intel TXT | TPM 2.0 Spec, TCG PC Client |
| [mini-boot-attestation](mini-boot-attestation/) | TPM Quote, remote attestation protocol, attestation key hierarchy, EK/AIK, Verifier service | TPM 2.0 Spec Part 1, TCG TAP |
| [mini-firmware-security](mini-firmware-security/) | SPI flash protection, BMC/ME security, SMM attacks, DMA attacks, firmware update capsules | NIST SP 800-193, DHS CISA |

## Design Philosophy

- **Zero external dependencies** — pure C (C99/C11), only `libc` and `libm`
- **Self-contained modules** — each directory has its own `Makefile`, `include/`, `src/`, `examples/`, `demos/`, `tests/`
- **Firmware simulation in user-space** — educational models of firmware behavior, boot flows, and security protocols
- **Theory-to-code mapping** — every module includes `docs/` with spec-alignment notes
- **Practical demos** — boot simulator, TPM emulator, secure boot verifier, device tree parser, and more

## Building

Each module is standalone. Navigate to a module directory and run:

```bash
cd mini-boot-process
make all    # build everything
make test   # run tests
```

Requires **GCC** and **GNU Make**.

## Project Structure

```
mini-firmware-boot/
├── mini-firmware/              # Firmware Architecture & Basics
├── mini-boot-process/          # Boot Process & Phases
├── mini-bios-uefi/             # BIOS & UEFI Firmware
├── mini-bootloader/            # Bootloaders (GRUB, U-Boot)
├── mini-hardware-desc/         # Hardware Description (DT, ACPI)
├── mini-secure-boot/           # UEFI Secure Boot
├── mini-measured-boot/         # TPM Measured Boot
├── mini-boot-attestation/      # Remote Attestation
└── mini-firmware-security/     # Firmware Security
```

## License

MIT
