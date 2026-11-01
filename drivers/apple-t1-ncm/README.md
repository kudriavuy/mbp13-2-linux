# apple-t1-ncm (DKMS)

The private CDC-NCM link between the host and the T1 (MacBookPro13,2), USB
configuration 2, interfaces 4/5: the T1's Secure Enclave reaches its xART
storage on the host over it (Touch ID, stage "Touch ID" step 1). The T1 omits
the interrupt/status endpoint generic `cdc_ncm` wants; this binds the mainline
CDC-NCM/usbnet code with flags matching the interface. It neither switches the
USB configuration nor touches the SEP.

`apple_t1_ncm.c` and `50-apple-t1-ncm.link` (from `systemd/50-t1bridge-ncm.link`)
are T1Bridge's (the `.link` with our `Name=t1ncm0` instead of `NamePolicy=keep`): https://github.com/standardagents/t1bridge commit
81cbdf8 (v0.1.12), `kernel/apple-t1-ncm/`; Copyright (c) 2026 T1Bridge
contributors, MIT (see LICENSE) or GPL-2.0-only. Only the DKMS packaging here
is ours.

The `.link` file names the interface `t1ncm0` and marks it unmanaged for
NetworkManager (`NM_UNMANAGED=1`): no DHCP, no routes, nothing but the
link-local IPv6 the T1 services use.

    sudo rsync -a --exclude .git ./ /usr/src/apple-t1-ncm-0.1/
    sudo dkms install apple-t1-ncm/0.1 -k <kernel>     # each kernel
    sudo install -m644 50-apple-t1-ncm.link /etc/systemd/network/

Undo: `sudo dkms remove apple-t1-ncm/0.1 --all`, remove the `.link` file.
