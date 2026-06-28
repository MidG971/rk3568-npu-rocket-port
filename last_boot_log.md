-Boot 2026.04-rc1-g6a1bdb7e952d (Jan 29 2026 - 10:21:41 +0000)

Model: Radxa ROCK 3B
SoC:   RK3568
DRAM:  8 GiB (total 7.7 GiB)
PMIC:  RK809 (on=0x40, off=0x00)
Core:  632 devices, 33 uclasses, devicetree: separate
MMC:   mmc@fe2b0000: 1, mmc@fe310000: 0
Loading Environment from nowhere... OK
In:    serial@fe660000
Out:   serial@fe660000
Err:   serial@fe660000
Model: Radxa ROCK 3B
SoC:   RK3568
Net:   eth1: ethernet@fe010000, eth0: ethernet@fe2a0000
Hit any key to stop autoboot: 0
Scanning for bootflows in all bootdevs
Seq  Method       State   Uclass    Part  Name                      Filename
---  -----------  ------  --------  ----  ------------------------  ----------------
Scanning global bootmeth 'efi_mgr':
Card did not respond to voltage select! : -110
  0  efi_mgr      ready   (none)       0  <NULL>
** Booting bootflow '<NULL>' with efi_mgr
Loading Boot0000 'mmc 0' failed
EFI boot manager: Cannot load any image
Boot failed (err=-14)
Scanning bootdev 'mmc@fe2b0000.bootdev':
Card did not respond to voltage select! : -110
Card did not respond to voltage select! : -110
Card did not respond to voltage select! : -110
Card did not respond to voltage select! : -110
Scanning bootdev 'mmc@fe310000.bootdev':
  1  extlinux     ready   mmc          3  mmc@fe310000.bootdev.part /boot/extlinux/extlinux.conf
** Booting bootflow 'mmc@fe310000.bootdev.part_3' with extlinux
U-Boot menu
1:      Debian GNU/Linux 11 (bullseye) 5.10.160-18-rk356x
2:      Debian GNU/Linux 11 (bullseye) 5.10.160-18-rk356x (rescue target)
3:      Debian GNU/Linux 6.19-rc5
Enter choice: 3:        Debian GNU/Linux 6.19-rc5
Retrieving file: /boot/Image-6.19-rc5
Retrieving file: /boot/initrd.img-6.19.0-rc5-00006-g76cd27aa0fe5-dirty
append: root=UUID=d7611020-2d80-4f32-b67d-5fb55de6949d rw loglevel=8 ignore_loglevel  earlycon console=ttyFIQ0,1500000n8 console=ttyS2,1500000n8
Retrieving file: /boot/rk3568-rock-3b-6.19-mainline.dtb
## Flattened Device Tree blob at 12000000
   Booting using the fdt blob at 0x12000000
Working FDT set to 12000000
   Loading Ramdisk to e96de000, end ecbff203 ... OK
   Loading Device Tree to 00000000ece8e000, end 00000000ecea0c18 ... OK
Working FDT set to ece8e000

Starting kernel ...

[    0.000000] Booting Linux on physical CPU 0x0000000000 [0x412fd050]
[    0.000000] Linux version 6.19.0-rc5-00006-g76cd27aa0fe5-dirty (radxa@rock-3b) (gcc (Debian 12.2.0-14+deb12u1) 12.2.0, GNU ld (GNU Binutils for Debian) 2.40) #13 SMP PREEMPT Tue Feb  3 11:17:25 UTC 2026
[    0.000000] KASLR enabled
[    0.000000] Machine model: Radxa ROCK 3B
[    0.000000] printk: debug: ignoring loglevel setting.
[    0.000000] efi: UEFI not found.
[    0.000000] earlycon: uart0 at MMIO32 0x00000000fe660000 (options '1500000n8')
[    0.000000] printk: legacy bootconsole [uart0] enabled
[    0.000000] OF: reserved mem: 0x000000000010f000..0x000000000010f0ff (0 KiB) nomap non-reusable shmem@10f000
[    0.000000] NUMA: Faking a node at [mem 0x0000000000200000-0x00000001ffffffff]
[    0.000000] NODE_DATA(0) allocated [mem 0x1ff01e640-0x1ff020cbf]
[    0.000000] Zone ranges:
[    0.000000]   DMA      [mem 0x0000000000200000-0x00000000ffffffff]
[    0.000000]   DMA32    empty
[    0.000000]   Normal   [mem 0x0000000100000000-0x00000001ffffffff]
[    0.000000] Movable zone start for each node
[    0.000000] Early memory node ranges
[    0.000000]   node   0: [mem 0x0000000000200000-0x00000000efffffff]
[    0.000000]   node   0: [mem 0x0000000100000000-0x00000001ffffffff]
[    0.000000] Initmem setup node 0 [mem 0x0000000000200000-0x00000001ffffffff]
[    0.000000] On node 0, zone DMA: 512 pages in unavailable ranges
[    0.000000] cma: Reserved 32 MiB at 0x00000000ee000000
[    0.000000] psci: probing for conduit method from DT.
[    0.000000] psci: PSCIv1.1 detected in firmware.
[    0.000000] psci: Using standard PSCI v0.2 function IDs
[    0.000000] psci: MIGRATE_INFO_TYPE not supported.
[    0.000000] psci: SMC Calling Convention v1.5
[    0.000000] percpu: Embedded 25 pages/cpu s63448 r8192 d30760 u102400
[    0.000000] pcpu-alloc: s63448 r8192 d30760 u102400 alloc=25*4096
[    0.000000] pcpu-alloc: [0] 0 [0] 1 [0] 2 [0] 3
[    0.000000] Detected VIPT I-cache on CPU0
[    0.000000] CPU features: detected: GICv3 CPU interface
[    0.000000] CPU features: detected: Virtualization Host Extensions
[    0.000000] CPU features: kernel page table isolation forced ON by KASLR
[    0.000000] CPU features: detected: Kernel page table isolation (KPTI)
[    0.000000] CPU features: detected: ARM errata 1165522, 1319367, or 1530923
[    0.000000] alternatives: applying boot alternatives
[    0.000000] Kernel command line: root=UUID=d7611020-2d80-4f32-b67d-5fb55de6949d rw loglevel=8 ignore_loglevel  earlycon console=ttyFIQ0,1500000n8 console=ttyS2,1500000n8
[    0.000000] printk: log buffer data + meta data: 131072 + 458752 = 589824 bytes
[    0.000000] Dentry cache hash table entries: 1048576 (order: 11, 8388608 bytes, linear)
[    0.000000] Inode-cache hash table entries: 524288 (order: 10, 4194304 bytes, linear)
[    0.000000] software IO TLB: area num 4.
[    0.000000] software IO TLB: mapped [mem 0x00000000e56de000-0x00000000e96de000] (64MB)
[    0.000000] Fallback order for Node 0: 0
[    0.000000] Built 1 zonelists, mobility grouping on.  Total pages: 2031104
[    0.000000] Policy zone: Normal
[    0.000000] mem auto-init: stack:off, heap alloc:off, heap free:off
[    0.000000] SLUB: HWalign=64, Order=0-3, MinObjects=0, CPUs=4, Nodes=1
[    0.000000] rcu: Preemptible hierarchical RCU implementation.
[    0.000000] rcu:     RCU event tracing is enabled.
[    0.000000] rcu:     RCU restricting CPUs from NR_CPUS=512 to nr_cpu_ids=4.
[    0.000000]  Trampoline variant of Tasks RCU enabled.
[    0.000000]  Tracing variant of Tasks RCU enabled.
[    0.000000] rcu: RCU calculated value of scheduler-enlistment delay is 25 jiffies.
[    0.000000] rcu: Adjusting geometry for rcu_fanout_leaf=16, nr_cpu_ids=4
[    0.000000] RCU Tasks: Setting shift to 2 and lim to 1 rcu_task_cb_adjust=1 rcu_task_cpu_ids=4.
[    0.000000] RCU Tasks Trace: Setting shift to 2 and lim to 1 rcu_task_cb_adjust=1 rcu_task_cpu_ids=4.
[    0.000000] NR_IRQS: 64, nr_irqs: 64, preallocated irqs: 0
[    0.000000] GIC: enabling workaround for GICv3: non-coherent attribute
[    0.000000] GICv3: GIC: Using split EOI/Deactivate mode
[    0.000000] GICv3: 320 SPIs implemented
[    0.000000] GICv3: 0 Extended SPIs implemented
[    0.000000] GICv3: MBI range [296:319]
[    0.000000] GICv3: Using MBI frame 0x00000000fd410000
[    0.000000] Root IRQ handler: gic_handle_irq
[    0.000000] GICv3: GICv3 features: 16 PPIs
[    0.000000] GICv3: GICD_CTLR.DS=0, SCR_EL3.FIQ=0
[    0.000000] GICv3: CPU0: found redistributor 0 region 0:0x00000000fd460000
[    0.000000] ITS [mem 0xfd440000-0xfd45ffff]
[    0.000000] GIC: enabling workaround for ITS: Rockchip erratum RK3568002
[    0.000000] GIC: enabling workaround for ITS: non-coherent attribute
[    0.000000] ITS@0x00000000fd440000: allocated 8192 Devices @410000 (indirect, esz 8, psz 64K, shr 0)
[    0.000000] ITS@0x00000000fd440000: allocated 32768 Interrupt Collections @420000 (flat, esz 2, psz 64K, shr 0)
[    0.000000] ITS: using cache flushing for cmd queue
[    0.000000] GICv3: using LPI property table @0x0000000000430000
[    0.000000] GIC: using cache flushing for LPI property table
[    0.000000] GICv3: CPU0: using allocated LPI pending table @0x0000000000440000
[    0.000000] rcu: srcu_init: Setting srcu_struct sizes based on contention.
[    0.000000] arch_timer: cp15 timer running at 24.00MHz (phys).
[    0.000000] clocksource: arch_sys_counter: mask: 0xffffffffffffff max_cycles: 0x588fe9dc0, max_idle_ns: 440795202592 ns
[    0.000001] sched_clock: 56 bits at 24MHz, resolution 41ns, wraps every 4398046511097ns
[    0.004343] Console: colour dummy device 80x25
[    0.004927] Calibrating delay loop (skipped), value calculated using timer frequency.. 48.00 BogoMIPS (lpj=96000)
[    0.005945] pid_max: default: 32768 minimum: 301
[    0.006756] Mount-cache hash table entries: 16384 (order: 5, 131072 bytes, linear)
[    0.007547] Mountpoint-cache hash table entries: 16384 (order: 5, 131072 bytes, linear)
[    0.011877] rcu: Hierarchical SRCU implementation.
[    0.012414] rcu:     Max phase no-delay instances is 1000.
[    0.013355] Timer migration: 1 hierarchy levels; 8 children per group; 1 crossnode level
[    0.014454] fsl-mc MSI: msi-controller@fd440000 domain created
[    0.020151] EFI services will not be available.
[    0.021126] smp: Bringing up secondary CPUs ...
[    0.022492] Detected VIPT I-cache on CPU1
[    0.022650] GICv3: CPU1: found redistributor 100 region 0:0x00000000fd480000
[    0.022675] GICv3: CPU1: using allocated LPI pending table @0x0000000000450000
[    0.022740] CPU1: Booted secondary processor 0x0000000100 [0x412fd050]
[    0.023884] Detected VIPT I-cache on CPU2
[    0.024028] GICv3: CPU2: found redistributor 200 region 0:0x00000000fd4a0000
[    0.024051] GICv3: CPU2: using allocated LPI pending table @0x0000000000460000
[    0.024102] CPU2: Booted secondary processor 0x0000000200 [0x412fd050]
[    0.025262] Detected VIPT I-cache on CPU3
[    0.025405] GICv3: CPU3: found redistributor 300 region 0:0x00000000fd4c0000
[    0.025428] GICv3: CPU3: using allocated LPI pending table @0x0000000000470000
[    0.025479] CPU3: Booted secondary processor 0x0000000300 [0x412fd050]
[    0.025695] smp: Brought up 1 node, 4 CPUs
[    0.033399] SMP: Total of 4 processors activated.
[    0.033868] CPU: All CPU(s) started at EL2
[    0.034274] CPU features: detected: 32-bit EL0 Support
[    0.034779] CPU features: detected: 32-bit EL1 Support
[    0.035286] CPU features: detected: Data cache clean to the PoU not required for I/D coherence
[    0.036128] CPU features: detected: Common not Private translations
[    0.036742] CPU features: detected: CRC32 instructions
[    0.037252] CPU features: detected: RCpc load-acquire (LDAPR)
[    0.037841] CPU features: detected: LSE atomic instructions
[    0.038392] CPU features: detected: Privileged Access Never
[    0.038939] CPU features: detected: PMUv3
[    0.039335] CPU features: detected: RAS Extension Support
[    0.039868] CPU features: detected: XNX
[    0.040250] CPU features: detected: Speculative Store Bypassing Safe (SSBS)
[    0.041003] alternatives: applying system-wide alternatives
[    0.046239] CPU features: detected: ICV_DIR_EL1 trapping
[    0.047357] Memory: 7759528K/8124416K available (18752K kernel code, 4926K rwdata, 13652K rodata, 12032K init, 691K bss, 327096K reserved, 32768K cma-reserved)
[    0.051718] devtmpfs: initialized
[    0.075174] clocksource: jiffies: mask: 0xffffffff max_cycles: 0xffffffff, max_idle_ns: 7645041785100000 ns
[    0.076182] posixtimers hash table entries: 2048 (order: 3, 32768 bytes, linear)
[    0.076981] futex hash table entries: 1024 (65536 bytes on 1 NUMA nodes, total 64 KiB, linear).
[    0.078406] 2G module region forced by RANDOMIZE_MODULE_REGION_FULL
[    0.079055] 0 pages in range for non-PLT usage
[    0.079063] 511712 pages in range for PLT usage
[    0.084837] DMI not present or invalid.
[    0.089473] NET: Registered PF_NETLINK/PF_ROUTE protocol family
[    0.091499] DMA: preallocated 1024 KiB GFP_KERNEL pool for atomic allocations
[    0.092571] DMA: preallocated 1024 KiB GFP_KERNEL|GFP_DMA pool for atomic allocations
[    0.093896] DMA: preallocated 1024 KiB GFP_KERNEL|GFP_DMA32 pool for atomic allocations
[    0.094744] audit: initializing netlink subsys (disabled)
[    0.095562] audit: type=2000 audit(0.084:1): state=initialized audit_enabled=0 res=1
[    0.100652] thermal_sys: Registered thermal governor 'step_wise'
[    0.100672] thermal_sys: Registered thermal governor 'power_allocator'
[    0.101426] cpuidle: using governor menu
[    0.102924] hw-breakpoint: found 6 breakpoint and 4 watchpoint registers.
[    0.103782] ASID allocator initialised with 32768 entries
[    0.109486] Serial: AMBA PL011 UART driver
[    0.140079] /vop@fe040000: Fixed dependency cycle(s) with /hdmi@fe0a0000
[    0.140859] /hdmi@fe0a0000: Fixed dependency cycle(s) with /vop@fe040000
[    0.157635] /pcie@fe260000: Fixed dependency cycle(s) with /pcie@fe260000/legacy-interrupt-controller
[    0.177123] gpio gpiochip0: Static allocation of GPIO base is deprecated, use dynamic allocation.
[    0.178814] rockchip-gpio fdd60000.gpio: probed /pinctrl/gpio@fdd60000
[    0.180270] gpio gpiochip1: Static allocation of GPIO base is deprecated, use dynamic allocation.
[    0.181497] rockchip-gpio fe740000.gpio: probed /pinctrl/gpio@fe740000
[    0.182926] gpio gpiochip2: Static allocation of GPIO base is deprecated, use dynamic allocation.
[    0.184125] rockchip-gpio fe750000.gpio: probed /pinctrl/gpio@fe750000
[    0.185684] gpio gpiochip3: Static allocation of GPIO base is deprecated, use dynamic allocation.
[    0.186921] rockchip-gpio fe760000.gpio: probed /pinctrl/gpio@fe760000
[    0.188271] gpio gpiochip4: Static allocation of GPIO base is deprecated, use dynamic allocation.
[    0.189454] rockchip-gpio fe770000.gpio: probed /pinctrl/gpio@fe770000
[    0.196460] /pcie@fe280000: Fixed dependency cycle(s) with /pcie@fe280000/legacy-interrupt-controller
[    0.200069] /hdmi@fe0a0000: Fixed dependency cycle(s) with /hdmi-con
[    0.200817] /hdmi-con: Fixed dependency cycle(s) with /hdmi@fe0a0000
[    0.211419] HugeTLB: registered 1.00 GiB page size, pre-allocated 0 pages
[    0.212104] HugeTLB: 0 KiB vmemmap can be freed for a 1.00 GiB page
[    0.212722] HugeTLB: registered 32.0 MiB page size, pre-allocated 0 pages
[    0.213386] HugeTLB: 0 KiB vmemmap can be freed for a 32.0 MiB page
[    0.214001] HugeTLB: registered 2.00 MiB page size, pre-allocated 0 pages
[    0.214665] HugeTLB: 0 KiB vmemmap can be freed for a 2.00 MiB page
[    0.215305] HugeTLB: registered 64.0 KiB page size, pre-allocated 0 pages
[    0.215971] HugeTLB: 0 KiB vmemmap can be freed for a 64.0 KiB page
[    0.220457] ACPI: Interpreter disabled.
[    0.229146] iommu: Default domain type: Translated
[    0.229639] iommu: DMA domain TLB invalidation policy: strict mode
[    0.230833] SCSI subsystem initialized
[    0.231489] libata version 3.00 loaded.
[    0.232282] usbcore: registered new interface driver usbfs
[    0.232885] usbcore: registered new interface driver hub
[    0.233456] usbcore: registered new device driver usb
[    0.236937] pps_core: LinuxPPS API ver. 1 registered
[    0.237435] pps_core: Software ver. 5.3.6 - Copyright 2005-2007 Rodolfo Giometti <giometti@linux.it>
[    0.238349] PTP clock support registered
[    0.239024] EDAC MC: Ver: 3.0.0
[    0.240273] scmi_core: SCMI protocol bus registered
[    0.243536] FPGA manager framework
[    0.245695] vgaarb: loaded
[    0.246825] clocksource: Switched to clocksource arch_sys_counter
[    0.247953] VFS: Disk quotas dquot_6.6.0
[    0.248376] VFS: Dquot-cache hash table entries: 512 (order 0, 4096 bytes)
[    0.249681] pnp: PnP ACPI: disabled
[    0.264806] NET: Registered PF_INET protocol family
[    0.265799] IP idents hash table entries: 131072 (order: 8, 1048576 bytes, linear)
[    0.274024] tcp_listen_portaddr_hash hash table entries: 4096 (order: 4, 65536 bytes, linear)
[    0.275058] Table-perturb hash table entries: 65536 (order: 6, 262144 bytes, linear)
[    0.275847] TCP established hash table entries: 65536 (order: 7, 524288 bytes, linear)
[    0.277147] TCP bind hash table entries: 65536 (order: 9, 2097152 bytes, linear)
[    0.279800] TCP: Hash tables configured (established 65536 bind 65536)
[    0.280618] UDP hash table entries: 4096 (order: 6, 262144 bytes, linear)
[    0.281600] UDP-Lite hash table entries: 4096 (order: 6, 262144 bytes, linear)
[    0.282897] NET: Registered PF_UNIX/PF_LOCAL protocol family
[    0.284192] RPC: Registered named UNIX socket transport module.
[    0.284782] RPC: Registered udp transport module.
[    0.285246] RPC: Registered tcp transport module.
[    0.285707] RPC: Registered tcp-with-tls transport module.
[    0.286247] RPC: Registered tcp NFSv4.1 backchannel transport module.
[    0.286931] PCI: CLS 0 bytes, default 64
[    0.287619] Unpacking initramfs...
[    0.292808] kvm [1]: nv: 568 coarse grained trap handlers
[    0.294050] kvm [1]: IPA Size Limit: 40 bits
[    0.294514] kvm [1]: GICv3: no GICV resource entry
[    0.295029] kvm [1]: disabling GICv2 emulation
[    0.295514] kvm [1]: GIC system register CPU interface enabled
[    0.296124] kvm [1]: vgic interrupt IRQ9
[    0.296582] kvm [1]: VHE mode initialized successfully
[    0.299770] Initialise system trusted keyrings
[    0.300594] workingset: timestamp_bits=42 max_order=21 bucket_order=0
[    0.301793] squashfs: version 4.0 (2009/01/31) Phillip Lougher
[    0.302962] NFS: Registering the id_resolver key type
[    0.303498] Key type id_resolver registered
[    0.303916] Key type id_legacy registered
[    0.304359] nfs4filelayout_init: NFSv4 File Layout Driver Registering...
[    0.305023] nfs4flexfilelayout_init: NFSv4 Flexfile Layout Driver Registering...
[    0.306220] 9p: Installing v9fs 9p2000 file system support
[    0.382501] Key type asymmetric registered
[    0.382954] Asymmetric key parser 'x509' registered
[    0.383573] Block layer SCSI generic (bsg) driver version 0.4 loaded (major 244)
[    0.384305] io scheduler mq-deadline registered
[    0.384755] io scheduler kyber registered
[    0.385226] io scheduler bfq registered
[    3.627025] Freeing initrd memory: 54404K
[    3.634943] ledtrig-cpu: registered to indicate activity on CPUs
[    3.644034] phy phy-fe8c0000.phy.0: lane number 0, val 1
[    3.645183] rockchip-dw-pcie 3c0800000.pcie: host bridge /pcie@fe280000 ranges:
[    3.645959] rockchip-dw-pcie 3c0800000.pcie:       IO 0x00f0100000..0x00f01fffff -> 0x00f0100000
[    3.646892] rockchip-dw-pcie 3c0800000.pcie:      MEM 0x00f0200000..0x00f1ffffff -> 0x00f0200000
[    3.647769] rockchip-dw-pcie 3c0800000.pcie:      MEM 0x0380000000..0x03bfffffff -> 0x0040000000
[    3.656071] rockchip-dw-pcie 3c0800000.pcie: iATU: unroll T, 8 ob, 8 ib, align 64K, limit 8G
[    3.759279] rockchip-dw-pcie 3c0800000.pcie: PCI host bridge to bus 0002:20
[    3.759982] pci_bus 0002:20: root bus resource [bus 20-2f]
[    3.760534] pci_bus 0002:20: root bus resource [io  0x0000-0xfffff] (bus address [0xf0100000-0xf01fffff])
[    3.761474] pci_bus 0002:20: root bus resource [mem 0xf0200000-0xf1ffffff]
[    3.762154] pci_bus 0002:20: root bus resource [mem 0x380000000-0x3bfffffff] (bus address [0x40000000-0x7fffffff])
[    3.763267] pci 0002:20:00.0: [1d87:3566] type 01 class 0x060400 PCIe Root Port
[    3.764018] pci 0002:20:00.0: BAR 0 [mem 0x00000000-0x3fffffff]
[    3.764611] pci 0002:20:00.0: BAR 1 [mem 0x00000000-0x3fffffff]
[    3.765199] pci 0002:20:00.0: ROM [mem 0x00000000-0x0000ffff pref]
[    3.765812] pci 0002:20:00.0: PCI bridge to [bus 01-ff]
[    3.766334] pci 0002:20:00.0:   bridge window [io  0x0000-0x0fff]
[    3.766964] pci 0002:20:00.0:   bridge window [mem 0x00000000-0x000fffff]
[    3.767646] pci 0002:20:00.0:   bridge window [mem 0x00000000-0x000fffff 64bit pref]
[    3.768487] pci 0002:20:00.0: supports D1 D2
[    3.768915] pci 0002:20:00.0: PME# supported from D0 D1 D3hot
[    3.774833] pci 0002:20:00.0: Primary bus is hard wired to 0
[    3.775407] pci 0002:20:00.0: bridge configuration invalid ([bus 01-ff]), reconfiguring
[    3.776511] pci_bus 0002:21: busn_res: [bus 21-2f] end is updated to 21
[    3.777203] pci 0002:20:00.0: BAR 0 [mem 0x380000000-0x3bfffffff]: assigned
[    3.777901] pci 0002:20:00.0: BAR 1 [mem size 0x40000000]: can't assign; no space
[    3.778641] pci 0002:20:00.0: BAR 1 [mem size 0x40000000]: failed to assign
[    3.779361] pci 0002:20:00.0: ROM [mem 0xf0200000-0xf020ffff pref]: assigned
[    3.780065] pci 0002:20:00.0: BAR 0 [mem 0x380000000-0x3bfffffff]: releasing
[    3.780762] pci 0002:20:00.0: ROM [mem 0xf0200000-0xf020ffff pref]: releasing
[    3.781470] pci 0002:20:00.0: BAR 0 [mem 0x380000000-0x3bfffffff]: assigned
[    3.782161] pci 0002:20:00.0: BAR 1 [mem size 0x40000000]: can't assign; no space
[    3.782920] pci 0002:20:00.0: BAR 1 [mem size 0x40000000]: failed to assign
[    3.783613] pci 0002:20:00.0: PCI bridge to [bus 21]
[    3.784116] pci_bus 0002:20: Some PCI device resources are unassigned, try booting with pci=realloc
[    3.785002] pci_bus 0002:20: resource 4 [io  0x0000-0xfffff]
[    3.785563] pci_bus 0002:20: resource 5 [mem 0xf0200000-0xf1ffffff]
[    3.786182] pci_bus 0002:20: resource 6 [mem 0x380000000-0x3bfffffff]
[    3.791651] pcieport 0002:20:00.0: PME: Signaling with IRQ 32
[    3.792761] pcieport 0002:20:00.0: AER: enabled with IRQ 33
[    3.855786] dma-pl330 fe530000.dma-controller: Loaded driver for PL330 DMAC-241330
[    3.856555] dma-pl330 fe530000.dma-controller:       DBUFF-128x8bytes Num_Chans-8 Num_Peri-32 Num_Events-16
[    3.860608] dma-pl330 fe550000.dma-controller: Loaded driver for PL330 DMAC-241330
[    3.861365] dma-pl330 fe550000.dma-controller:       DBUFF-128x8bytes Num_Chans-8 Num_Peri-32 Num_Events-16
[    3.898473] Serial: 8250/16550 driver, 4 ports, IRQ sharing enabled
[    3.904242] pci 0002:21:00.0: [10ec:5765] type 00 class 0x010802 PCIe Endpoint
[    3.905188] pci 0002:21:00.0: BAR 0 [mem 0x00000000-0x00003fff 64bit]
[    3.905858] pci 0002:21:00.0: BAR 5 [mem 0x00000000-0x00001fff]
[    3.905898] printk: legacy console [ttyS2] disabled
[    3.907253] pci 0002:21:00.0: 15.752 Gb/s available PCIe bandwidth, limited by 8.0 GT/s PCIe x2 link at 0002:20:00.0 (capable of 31.504 Gb/s with 8.0 GT/s PCIe x4 link)
[    3.908770] fe660000.serial: ttyS2 at MMIO 0xfe660000 (irq = 38, base_baud = 1500000) is a 16550A
[    3.909759] printk: legacy console [ttyS2] enabled
[    3.909759] printk: legacy console [ttyS2] enabled
[    3.910663] printk: legacy bootconsole [uart0] disabled
[    3.910663] printk: legacy bootconsole [uart0] disabled
[    3.914906] pci 0002:21:00.0: ASPM: default states L1
[    3.915501] pcieport 0002:20:00.0: bridge window [mem 0xf0200000-0xf02fffff]: assigned
[    3.916221] pci 0002:21:00.0: BAR 0 [mem 0xf0200000-0xf0203fff 64bit]: assigned
[    3.916918] pci 0002:21:00.0: BAR 5 [mem 0xf0204000-0xf0205fff]: assigned
[    3.923548] msm_serial: driver initialized
[    3.925063] SuperH (H)SCI(F) driver initialized
[    3.926099] STM32 USART driver initialized
[    3.936810] platform fdea0000.video-codec: Adding to iommu group 0
[    3.936933] random: crng init done
[    3.939197] platform fdee0000.video-codec: Adding to iommu group 1
[    3.941344] platform fe040000.vop: Adding to iommu group 2
[    3.943026] Error: Driver 'efi-framebuffer' is already registered, aborting...
[    3.954952] loop: module loaded
[    3.958287] megasas: 07.734.00.00-rc1
[    3.961309] nvme nvme0: pci function 0002:21:00.0
[    3.961776] nvme 0002:21:00.0: enabling device (0000 -> 0002)
[    3.978132] tun: Universal TUN/TAP device driver, 1.6
[    3.981963] thunder_xcv, ver 1.0
[    3.982358] thunder_bgx, ver 1.0
[    3.982734] nicpf, ver 1.0
[    3.987080] e1000: Intel(R) PRO/1000 Network Driver
[    3.987533] e1000: Copyright (c) 1999-2006 Intel Corporation.
[    3.988143] e1000e: Intel(R) PRO/1000 Network Driver
[    3.988589] e1000e: Copyright(c) 1999 - 2015 Intel Corporation.
[    3.989192] igb: Intel(R) Gigabit Ethernet Network Driver
[    3.989677] igb: Copyright (c) 2007-2014 Intel Corporation.
[    3.990242] igbvf: Intel(R) Gigabit Virtual Function Network Driver
[    3.990800] igbvf: Copyright (c) 2009 - 2012 Intel Corporation.
[    3.992454] sky2: driver version 1.30
[    3.997174] VFIO - User Level meta-driver version: 0.3
[    4.008663] usbcore: registered new interface driver usb-storage
[    4.019332] i2c_dev: i2c /dev entries driver
[    4.025799] fan53555-regulator 0-001c: FAN53555 Option[12] Rev[15] Detected!
[    4.051084] sdhci: Secure Digital Host Controller Interface driver
[    4.051656] sdhci: Copyright(c) Pierre Ossman
[    4.054568] Synopsys Designware Multimedia Card Interface Driver
[    4.058513] sdhci-pltfm: SDHCI platform and OF driver helper
[    4.069157] arm-scmi arm-scmi.0.auto: Using scmi_smc_transport
[    4.069702] arm-scmi arm-scmi.0.auto: SCMI max-rx-timeout: 30ms / max-msg-size: 104bytes / max-msg: 20
[    4.070748] scmi_protocol scmi_dev.1: Enabled polling mode TX channel - prot_id:16
[    4.071897] arm-scmi arm-scmi.0.auto: SCMI Notifications - Core Enabled.
[    4.072567] arm-scmi arm-scmi.0.auto: SCMI Protocol v2.0 'rockchip:' Firmware version 0x0
[    4.073403] arm-scmi arm-scmi.0.auto: Enabling SCMI Quirk [quirk_clock_rates_triplet_out_of_spec]
[    4.075877] SMCCC: SOC_ID: ARCH_SOC_ID not implemented, skipping ....
[    4.080959] usbcore: registered new interface driver usbhid
[    4.081471] usbhid: USB HID core driver
[    4.092653] hw perfevents: enabled with armv8_cortex_a55 PMU driver, 7 (0,8000003f) counters available
[    4.109855] NET: Registered PF_PACKET protocol family
[    4.110505] 9pnet: Installing 9P2000 support
[    4.111040] Key type dns_resolver registered
[    4.122521] nvme nvme0: allocated 64 MiB host memory buffer (16 segments).
[    4.133456] registered taskstats version 1
[    4.134035] Loading compiled-in X.509 certificates
[    4.149291] Demotion targets for Node 0: null
[    4.158011] nvme nvme0: 4/0/0 default/read/poll queues
[    4.180924] nvme nvme0: Ignoring bogus Namespace Identifiers
[    4.221979] ehci-platform fd800000.usb: EHCI Host Controller
[    4.222355] ohci-platform fd840000.usb: Generic Platform OHCI controller
[    4.222553] ehci-platform fd800000.usb: new USB bus registered, assigned bus number 1
[    4.223191] ohci-platform fd840000.usb: new USB bus registered, assigned bus number 2
[    4.224028] ehci-platform fd800000.usb: irq 54, io mem 0xfd800000
[    4.224656] ohci-platform fd840000.usb: irq 80, io mem 0xfd840000
[    4.234879] ehci-platform fd800000.usb: USB 2.0 started, EHCI 1.00
[    4.236644] hub 1-0:1.0: USB hub found
[    4.237103] hub 1-0:1.0: 1 port detected
[    4.283795] hub 2-0:1.0: USB hub found
[    4.284194] hub 2-0:1.0: 1 port detected
[    4.315039] vdda0v9_image: Bringing 600000uV into 900000-900000uV
[    4.342763] vcca1v8_image: Bringing 600000uV into 1800000-1800000uV
[    4.369425] dwmmc_rockchip fe2b0000.mmc: IDMAC supports 32-bit address mode.
[    4.370104] dwmmc_rockchip fe2b0000.mmc: Using internal DMA controller.
[    4.370696] dwmmc_rockchip fe2b0000.mmc: Version ID is 270a
[    4.371309] dwmmc_rockchip fe2b0000.mmc: DW MMC controller at irq 81,32 bit host data width,256 deep fifo
[    4.392226] panfrost fde60000.gpu: clock rate = 594000000
[    4.392732] panfrost fde60000.gpu: bus_clock rate = 500000000
[    4.393300] mmc_host mmc1: Bus speed (slot 0) = 375000Hz (slot req 400000Hz, actual 375000HZ div = 0)
[    4.400675] panfrost fde60000.gpu: mali-g52 id 0x7402 major 0x1 minor 0x0 status 0x0
[    4.401363] panfrost fde60000.gpu: features: 00000000,00000df7, issues: 00000000,00000400
[    4.402080] panfrost fde60000.gpu: Features: L2:0x07110206 Shader:0x00000002 Tiler:0x00000209 Mem:0x1 MMU:0x00002823 AS:0xff JS:0x7
[    4.403160] panfrost fde60000.gpu: shader_present=0x1 l2_present=0x1
[    4.404856] [drm] Initialized panfrost 1.5.0 for fde60000.gpu on minor 0
[    4.410875] mmc0: SDHCI controller on fe310000.mmc [fe310000.mmc] using ADMA
[    4.474124] mmc0: new HS200 MMC card at address 0001
[    4.479007] mmcblk0: mmc0:0001 BJTD4R 29.1 GiB
[    4.481961]  mmcblk0: p1 p2 p3
[    4.482795] mmcblk0boot0: mmc0:0001 BJTD4R 4.00 MiB
[    4.485959] mmcblk0boot1: mmc0:0001 BJTD4R 4.00 MiB
[    4.489620] mmcblk0rpmb: mmc0:0001 BJTD4R 4.00 MiB, chardev (509:0)
[    4.490856] usb 1-1: new high-speed USB device number 2 using ehci-platform
[    4.643929] hub 1-1:1.0: USB hub found
[    4.644339] hub 1-1:1.0: 4 ports detected
[    9.591350] rockchip-pm-domain fdd90000.power-management:power-controller: failed to get ack on domain 'npu', val=0x1fe
[    9.592767] clk: Disabling unused clocks
[    9.595035] PM: genpd: Disabling unused power domains
[    9.595716] dw-apb-uart fe660000.serial: forbid DMA for kernel console
[    9.600418] Freeing unused kernel memory: 12032K
[    9.600928] Run /init as init process
[    9.601255]   with arguments:
[    9.601443] RKNPU fde40000.npu: RKNPU: rknpu iommu device-tree entry not found!, using non-iommu mode
[    9.601517]     /init
[    9.601519]   with environment:
[    9.602795]     HOME=/
[    9.602883] [drm] Initialized rknpu 0.9.8 for fde40000.npu on minor 1
[    9.603033]     TERM=linux
[    9.604336] RKNPU fde40000.npu: PMU pwr_status=0x000000fd
Loading, please wait...
[    9.619617] RKNPU fde40000.npu: De-idle timeout: ack=0x000001fe idle=0x000001fe
[    9.620476] Internal error: synchronous external abort: 0000000096000010 [#1]  SMP
[    9.621154] Modules linked in:
[    9.621434] CPU: 3 UID: 0 PID: 12 Comm: kworker/u16:0 Tainted: G   M                6.19.0-rc5-00006-g76cd27aa0fe5-dirty #13 PREEMPT
[    9.622483] Tainted: [M]=MACHINE_CHECK
[    9.622814] Hardware name: Radxa ROCK 3B (DT)
[    9.623200] Workqueue: events_unbound deferred_probe_work_func
[    9.623730] pstate: 204000c9 (nzCv daIF +PAN -UAO -TCO -DIT -SSBS BTYPE=--)
[    9.624342] pc : regmap_mmio_read32le+0x8/0x20
[    9.624741] lr : regmap_mmio_read+0x44/0x70
[    9.625112] sp : ffff800080163690
[    9.625404] x29: ffff800080163690 x28: 0000000000000000 x27: 0000000000000000
[    9.626035] x26: 0000000000000000 x25: 0000000000000000 x24: ffff0001001d9298
[    9.626664] x23: 0000000000000000 x22: ffff0001009b4400 x21: ffff000100941a40
[    9.627295] x20: 0000000000000008 x19: ffff00010093d980 x18: ffff800081083c20
[    9.627925] x17: 0000000000000000 x16: 0000000000000000 x15: 0000000000000000
[    9.628553] x14: 0000000000000204 x13: 0000000000000001 x12: 0000000000000001
[    9.629183] x11: 00000000000000c0 x10: ffff0001000103e8 x9 : ffff800080163640
[    9.629812] x8 : 0000000000000000 x7 : 000fffffffe00000 x6 : ffff00010020adc0
[    9.630441] x5 : ffffa048276827f8 x4 : 0000000000000008 x3 : 0000000000000008
[    9.631071] x2 : ffffa04827682f54 x1 : 0000000000000008 x0 : ffff80008013d008
[    9.631702] Call trace:
[    9.631921]  regmap_mmio_read32le+0x8/0x20 (P)
[    9.632330]  _regmap_bus_reg_read+0x70/0xb0
[    9.632720]  _regmap_read+0x64/0xdc
[    9.633035]  regmap_read+0x4c/0x78
[    9.633341]  rockchip_pd_power+0xd4/0x5d8
[    9.633699]  rockchip_pd_power_off+0x1c/0x60
[    9.634080]  _genpd_power_off+0x98/0x154
[    9.634430]  genpd_power_off.part.0+0x1a4/0x29c
[    9.634833]  genpd_runtime_suspend+0x130/0x298
[    9.635229]  __rpm_callback+0x48/0x1dc
[    9.635562]  rpm_callback+0x74/0x80
[    9.635871]  rpm_suspend+0x108/0x570
[    9.636187]  rpm_idle+0x11c/0x1ac
[    9.636481]  __pm_runtime_idle+0x50/0x94
[    9.636828]  rknpu_power_off+0x20/0xa8
[    9.637163]  rknpu_probe+0x504/0x750
[    9.637480]  platform_probe+0x5c/0x98
[    9.637805]  really_probe+0xbc/0x298
[    9.638123]  __driver_probe_device+0x78/0x12c
[    9.638507]  driver_probe_device+0x3c/0x15c
[    9.638876]  __device_attach_driver+0xb8/0x134
[    9.639279]  bus_for_each_drv+0x84/0xe4
[    9.639642]  __device_attach+0xa0/0x190
[    9.639985]  device_initial_probe+0x50/0x54
[    9.640366]  bus_probe_device+0x38/0xa4
[    9.640719]  deferred_probe_work_func+0x88/0xc0
[    9.641131]  process_one_work+0x14c/0x290
[    9.641492]  worker_thread+0x2cc/0x3d4
[    9.641826]  kthread+0x130/0x20439-1~deb12u1

[    9.642487]  ret_from_fork+0x10/0x20
[    9.642820] Code: aa0603e0 d65f03c0 f9400000 8b214000 (b9400000)
[    9.643362] ---[ end trace 0000000000000000 ]---
[    9.643773] note: kworker/u16:0[12] exited with irqs disabled
[    9.644329] note: kworker/u16:0[12] exited with preempt_count 1
[   10.323439] rk808-rtc rk808-rtc.4.auto: registered as rtc0
[   10.328821] rk808-rtc rk808-rtc.4.auto: setting system clock to 2017-08-05T09:00:11 UTC (1501923611)
[   10.355450] rk_gmac-dwmac fe010000.ethernet: IRQ eth_lpi not found
[   10.356025] rk_gmac-dwmac fe010000.ethernet: IRQ sfty not found
[   10.357365] rk_gmac-dwmac fe010000.ethernet: clock input or output? (input).
[   10.358008] rk_gmac-dwmac fe010000.ethernet: Can not read property: tx_delay.
1;-1fdone.
Begin: Mounting root file system ... Begin: Running /scripts/local-top ... done.
Begin: Running /scripts/local-premount ... done.
Timed out for waiting the udev queue being empty.
Begin: Will now check root file system ... fsck from util-linux 2.38.1
[/sbin/fsck.ext4 (1) -- /dev/mmcblk0p3] fsck.ext4 -a -C0 /dev/mmcblk0p3
rootfs: recovering journal
rootfs: clean, 585944/1855392 files, 5352924/7548923 blocks
done.
[  141.200654] EXT4-fs (mmcblk0p3): mounted filesystem d7611020-2d80-4f32-b67d-5fb55de6949d r/w with ordered data mode. Quota mode: none.
done.
Begin: Running /scripts/local-bottom ... done.
Begin: Running /scripts/init-bottom ... done.
[  141.716319] systemd[1]: System time before build time, advancing clock.
[  141.806120] NET: Registered PF_INET6 protocol family
[  141.808225] Segment Routing with IPv6
[  141.808619] In-situ OAM (IOAM) with IPv6
[  141.899861] systemd[1]: systemd 252.39-1~deb12u1 running in system mode (+PAM +AUDIT +SELINUX +APPARMOR +IMA +SMACK +SECCOMP +GCRYPT -GNUTLS +OPENSSL +ACL +BLKID +CURL +ELFUTILS +FIDO2 +IDN2 -IDN +IPTC +KMOD +LIBCRYPTSETU)
[  141.902990] systemd[1]: Detected architecture arm64.

Welcome to Debian GNU/Linux 12 (bookworm)!

[  141.913369] systemd[1]: Hostname set to <rock-3b>.
[  142.080816] systemd-fstab-generator[207]: Failed to create unit file '/run/systemd/generator/mnt-nvme.mount', as it already exists. Duplicate entry in '/etc/fstab'?
[  142.093852] (sd-execut[204]: /usr/lib/systemd/system-generators/systemd-fstab-generator failed with exit status 1.
[  142.633977] systemd[1]: Queued start job for default target graphical.target.
[  142.667556] systemd[1]: Created slice system-getty.slice - Slice /system/getty.
[  OK  ] Created slice system-getty.slice - Slice /system/getty.
[  142.670328] systemd[1]: Created slice system-modprobe.slice - Slice /system/modprobe.
[  OK  ] Created slice system-modpr…lice - Slice /system/modprobe.
[  142.672527] systemd[1]: Created slice system-serial\x2dgetty.slice - Slice /system/serial-getty.
[  OK  ] Created slice system-seria… - Slice /system/serial-getty.
[  142.674287] systemd[1]: Created slice user.slice - User and Session Slice.
[  OK  ] Created slice user.slice - User[  142.675278] systemd[1]: Started systemd-ask-password-wall.path - Forward Password Requests to Wall Directory Watch.
 and Session Slice.
[  OK  ] Started systemd-ask-passwo… Reque[  142.676709] systemd[1]: proc-sys-fs-binfmt_misc.automount - Arbitrary Executable File Formats File System Automount Point was skipped because of an unmet condition check (Conditio.
sts to Wall Directory Watch.
[  142.678864] systemd[1]: Expecting device dev-ttyS2.device - /dev/ttyS2...
         Expecting device dev-ttyS2.device - /dev/t[  142.679748] systemd[1]: Reached target integritysetup.target - Local Integrity Protected Volumes.
tyS2...
[  OK  ] Reached target integrityse…Local [  142.681063] systemd[1]: Reached target nss-user-lookup.target - User and Group Name Lookups.
Integrity Protected Volumes.
[  142.682312] systemd[1]: Reached target remote-cryptsetup.target - Remote Encrypted Volumes.
[  OK  ] Reached target nss-user-lo…[0m - [  142.683294] systemd[1]: Reached target remote-fs.target - Remote File Systems.
User and Group Name Lookups.
[  OK  ] Reached targ[  142.684380] systemd[1]: Reached target slices.target - Slice Units.
et remote-cryp…et - Remote Encrypted Volumes.
[  142.685432] systemd[1]: Reached target swap.target - Swaps.
[  OK  ] Reached target remote-fs.target[  142.686421] systemd[1]: Reached target veritysetup.target - Local Verity Protected Volumes.
 - Remote File Systems.
[  OK  ] Reached target slices.target - Slice Units.
[  OK  ] Reached target swap.target - Swaps.
[  OK [  142.688575] systemd[1]: Listening on systemd-fsckd.socket - fsck to fsckd communication Socket.
 ] Reached target veritysetup… - Local Verity Pro[  142.690067] systemd[1]: Listening on systemd-initctl.socket - initctl Compatibility Named Pipe.
tected Volumes.
[  OK  ] Listening on systemd-fsckd…sck to fsckd communication Socket.
[  OK  ] Listening on systemd-initc… initctl Compatibil[  142.691942] systemd[1]: Listening on systemd-journald-audit.socket - Journal Audit Socket.
ity Named Pipe.
[  OK  ] Listening on systemd-journ…socket - Journal Audit Socket.
[  142.693694] systemd[1]: Listening on systemd-journald-dev-log.socket - Journal Socket (/dev/log).
[  OK  ] Listening on systemd-journ…t - Journal Socket (/dev/log).
[  142.695395] systemd[1]: Listening on systemd-journald.socket - Journal Socket.
[  OK  ] Listening on systemd-journald.socket - Journal Socket.
[  142.696925] systemd[1]: Listening on systemd-networkd.socket - Network Service Netlink Socket.
[  OK  ] Listening on systemd-netwo… - Network Service Netlink Socket.
[  142.700402] systemd[1]: Listening on systemd-udevd-control.socket - udev Control Socket.
[  OK  ] Listening on systemd-udevd….socke[  142.701641] systemd[1]: Listening on systemd-udevd-kernel.socket - udev Kernel Socket.
t - udev Control Socket.
[  OK  ] Listening on systemd-udevd…l.socket - udev Kernel Socket.
[  142.705515] systemd[1]: Mounting dev-hugepages.mount - Huge Pages File System...
         Mounting dev-hugepages.mount - Huge Pages File System...
[  142.709689] systemd[1]: Mounting dev-mqueue.mount - POSIX Message Queue File System...
         Mounting dev-mqueue.mountPOSIX Message Queue File System...
[  142.714274] systemd[1]: Mounting sys-kernel-debug.mount - Kernel Debug File System...
         Mounting sys-kernel-debug.… - Kernel Deb[  142.715676] systemd[1]: sys-kernel-tracing.mount - Kernel Trace File System was skipped because of an unmet condition check (ConditionPathExists=/sys/kernel/tracing).
ug File System...
[  142.721339] systemd[1]: Starting ifupdown-wait-online.service - Wait for network to be configured by ifupdown...
         Starting ifupdown-wait-onl…rk to be configured by ifupdown...
[  142.726370] systemd[1]: Starting kmod-static-nodes.service - Create List of Static Device Nodes...
         Starting kmod-static-nodes…ate List of Static Device Nodes...
[  142.732212] systemd[1]: Starting modprobe@configfs.service - Load Kernel Module configfs...
         Starting modprobe@configfs…m - Load Kernel Module configfs...
[  142.737843] systemd[1]: Starting modprobe@dm_mod.service - Load Kernel Module dm_mod...
         Starting modprobe@dm_mod.s…[0m - Load Kernel Module dm_mod...
[  142.743746] systemd[1]: Starting modprobe@drm.service - Load Kernel Module drm...
         Starting modprobe@drm.service - Load Kernel Module drm...
[  142.748688] systemd[1]: Starting modprobe@efi_pstore.service - Load Kernel Module efi_pstore...
         Starting modprobe@efi_psto…- Load Kernel Module efi_pstore...
[  142.754756] systemd[1]: Starting modprobe@fuse.service - Load Kernel Module fuse...
         Starting modprobe@fuse.ser…e - Load Kernel Module fuse...
[  142.760745] systemd[1]: Starting modprobe@loop.service - Load Kernel Module loop...
[  142.761478] device-mapper: ioctl: 4.50.0-ioctl (2025-04-28) initialised: dm-devel@lists.linux.dev
         Starting modprobe@loop.ser…e - Load Kernel Module loop...
[  142.763467] systemd[1]: systemd-fsck-root.service - File System Check on Root Device was skipped because of an unmet condition check (ConditionPathExists=!/run/initramfs/fsck-root).
[  142.772634] systemd[1]: Starting systemd-journald.service - Journal Service...
         Starting systemd-journald.service - Journal Service...
[  142.780368] fuse: init (API version 7.45)
[  142.784145] systemd[1]: Starting systemd-modules-load.service - Load Kernel Modules...
         Starting systemd-modules-l…rvice - Load Kernel Modules...
[  142.789124] systemd[1]: Starting systemd-remount-fs.service - Remount Root and Kernel File Systems...
         Starting systemd-remount-f…nt Root and Kernel File Systems...
[  142.794756] systemd[1]: Starting systemd-udev-trigger.service - Coldplug All udev Devices...
         Starting systemd-udev-trig…[0m - Coldplug All udev Devices...
[  142.802973] systemd[1]: Mounted dev-hugepages.mount - Huge Pages File System.
[  OK  ] Mounted dev-hugepages.mount - Huge Pages File System.
[  142.804895] systemd[1]: Mounted dev-mqueue.mount - POSIX Message Queue File System.
[  OK  ] Mounted dev-mqueue.mount- POSIX Message Queue File System.
[  142.807154] systemd[1]: Mounted sys-kernel-debug.mount - Kernel Debug File System.
[  OK  ] Mounted sys-kernel-debug.m…nt - Kernel Debug File System.
[  142.809622] systemd[1]: Finished ifupdown-wait-online.service - Wait for network to be configured by ifupdown.
[  OK  ] Finished ifupdown-wait-onl…work to be configured by ifupdown.
[  142.812644] systemd[1]: Finished kmod-static-nodes.service - Create List of Static Device Nodes.
[  OK  ] Finished kmod-static-nodes…reate List of Static Device Nodes.
[  142.815302] systemd[1]: modprobe@configfs.service: Deactivated successfully.
[  142.816642] systemd[1]: Finished modprobe@configfs.service - Load Kernel Module configfs.
[  OK  ] Finished modprobe@configfs…[0m - Load Kernel Module configfs.
[  142.818953] systemd[1]: modprobe@dm_mod.service: Deactivated successfully.
[  142.820520] systemd[1]: Finished modprobe@dm_mod.service - Load Kernel Module dm_mod.
[  OK  ] Finished modprobe@dm_mod.s…e - Load Kernel Module dm_mod.
[  142.823386] systemd[1]: modprobe@drm.service: Deactivated successfully.
[  142.824857] systemd[1]: Finished modprobe@drm.service - Load Kernel Module drm.
[  OK  ] Finished modprobe@drm.service - Load Kernel Module drm.
[  142.827333] systemd[1]: modprobe@efi_pstore.service: Deactivated successfully.
[  142.828778] systemd[1]: Finished modprobe@efi_pstore.service - Load Kernel Module efi_pstore.
[  OK  ] Finished modprobe@efi_psto…m - Load Kernel Module efi_pstore.
[  142.831454] systemd[1]: modprobe@fuse.service: Deactivated successfully.
[  142.832836] systemd[1]: Finished modprobe@fuse.service - Load Kernel Module fuse.
[  OK  ] Finished modprobe@fuse.service - Load Kernel Module fuse.
[  142.835390] systemd[1]: modprobe@loop.service: Deactivated successfully.
[  142.836718] systemd[1]: Finished modprobe@loop.service - Load Kernel Module loop.
[  OK  ] Finished modprobe@loop.service - Load Kernel Module loop.
[  142.838770] systemd[1]: Finished systemd-modules-load.service - Load Kernel Modules.
[  OK  ] Finished systemd-modules-l…service - Load Kernel Modules.
[  142.844502] systemd[1]: Mounting sys-fs-fuse-connections.mount - FUSE Control File System...
         Mounting sys-fs-fuse-conne… - FUSE Control File System...
[  142.846939] EXT4-fs (mmcblk0p3): re-mounted d7611020-2d80-4f32-b67d-5fb55de6949d.
[  142.849005] systemd[1]: Mounting sys-kernel-config.mount - Kernel Configuration File System...
         Mounting sys-kernel-config…ernel Configurati[  142.850453] systemd[1]: systemd-repart.service - Repartition Root Disk was skipped because no trigger condition checks were met.
on File System...
[  142.856432] systemd[1]: Starting systemd-sysctl.service - Apply Kernel Variables...
         Starting systemd-sysctl.se…ce - Apply Kernel Variables...
[  142.866924] systemd[1]: Finished systemd-remount-fs.service - Remount Root and Kernel File Systems.
[  OK  ] Finished systemd-remount-f…ount Root and Kernel File Systems.
[  142.873756] systemd[1]: Mounted sys-fs-fuse-connections.mount - FUSE Control File System.
[  OK  ] Mounted sys-fs-fuse-connec…nt - FUSE Control File System.
[  142.876321] systemd[1]: Mounted sys-kernel-config.mount - Kernel Configuration File System.
[  OK  ] Mounted sys-kernel-config.… Kernel Configuration File System.
[  142.878588] systemd[1]: systemd-firstboot.service - First Boot Wizard was skipped because of an unmet condition check (ConditionFirstBoot=yes).
[  142.880516] systemd[1]: systemd-pstore.service - Platform Persistent Storage Archival was skipped because of an unmet condition check (ConditionDirectoryNotEmpty=/sys/fs/pstore).
[  142.885667] systemd[1]: Starting systemd-random-seed.service - Load/Save Random Seed...
         Starting systemd-random-se…ice - Load/Save Random Seed...
[  142.890946] systemd[1]: Starting systemd-sysusers.service - Create System Users...
         Starting systemd-sysusers.…rvice - Create System Users...
[  142.909576] systemd[1]: Finished systemd-sysctl.service - Apply Kernel Variables.
[  OK  ] Finished systemd-sysctl.service - Apply Kernel Variables.
[  142.929359] systemd[1]: Finished systemd-random-seed.service - Load/Save Random Seed.
[  OK  ] Finished systemd-random-se…rvice - Load/Save Random Seed.
[  142.931826] systemd[1]: first-boot-complete.target - First Boot Complete was skipped because of an unmet condition check (ConditionFirstBoot=yes).
[  142.949885] systemd[1]: Finished systemd-sysusers.service - Create System Users.
[  OK  ] Finished systemd-sysusers.service - Create System Users.
[  142.979466] systemd[1]: Starting systemd-tmpfiles-setup-dev.service - Create Static Device Nodes in /dev...
         Starting systemd-tmpfiles-…ate Static Device Nodes in /dev...
[  142.981196] systemd[1]: Started systemd-journald.service - Journal Service.
[  OK  ] Started systemd-journald.service - Journal Service.
         Starting systemd-journal-f…h Journal to Persistent Storage...
[  143.016146] systemd-journald[232]: Received client request to flush runtime journal.
[  143.019233] systemd-journald[232]: File /var/log/journal/053fec86c513471db4cbcdd6d6bc2c4e/system.journal corrupted or uncleanly shut down, renaming and replacing.
[  OK  ] Finished systemd-tmpfiles-…reate Static Device Nodes in /dev.
[  OK  ] Reached target local-fs-pr…reparation for Local File Systems.
[  OK  ] Set up automount boot-efi.automount.
[  OK  ] Set up automount config.automount.
[  OK  ] Set up automount mnt-nvme.automount.
[  OK  ] Reached target local-fs.target - Local File Systems.
         Starting plymouth-read-wri…mouth To Write Out Runtime Data...
         Starting systemd-udevd.ser…ger for Device Events and Files...
[  OK  ] Finished plymouth-read-wri…lymouth To Write Out Runtime Data.
[  OK  ] Finished systemd-journal-f…ush Journal to Persistent Storage.
         Starting systemd-tmpfiles-…te System Files and Directories...
[  OK  ] Finished systemd-tmpfiles-…eate System Files and Directories.
[  OK  ] Started haveged.servicemon based on the HAVEGE algorithm.
         Starting systemd-timesyncd… - Network Time Synchronization...
         Starting systemd-update-ut…rd System Boot/Shutdown in UTMP...
[  OK  ] Started systemd-udevd.serv…nager for Device Events and Files.
         Starting systemd-networkd.…ice - Network Configuration...
[  OK  ] Finished systemd-update-ut…cord System Boot/Shutdown in UTMP.
[  143.531069] cfg80211: Loading compiled-in X.509 certificates for regulatory database
[  OK  ] Started systemd-timesyncd.…0m - Network Time Synchronization.
[  OK  ] Reached target time-set.target - System Time Set.
[  OK  ] Finished systemd-udev-trig…e - Coldplug All udev Devices.
         Starting ifupdown-pre.serv…ynchronize boot up for ifupdown...
         Starting plymouth-start.se…[0m - Show Plymouth Boot Screen...
[  OK  ] Finished ifupdown-pre.serv… synchronize boot up for ifupdown.
         Starting networking.service - Raise networ[  143.605642] Loaded X.509 cert 'sforshee: 00b28ddf47aef9cea7'
k interfaces...
[  143.607021] Loaded X.509 cert 'wens: 61c038651aabdcf94bd0ac7ff06c7248db18c600'
[  OK  ] Started systemd-networkd.service - Network Configuration.
         Starting systemd-networkd-…it for Network to be Configured...
[  OK  ] Started plymouth-start.ser…e - Show Plymouth Boot Screen.
[  OK  ] Started systemd-ask-passwo…uests to Plymouth Directory Watch.
[  OK  ] Reached target cryptsetup.…get - Local Encrypted Volumes.
[  OK  ] Reached target sysinit.target - System Initialization.
[  OK  ] Started cups.path - CUPS Scheduler.
[  OK  ] Started apt-daily.timer - Daily apt download activities.
[  OK  ] Started apt-daily-upgrade.… apt upgrade and clean activities.
[  OK  ] Started dpkg-db-backup.tim… Daily dpkg database backup timer.
[  OK  ] Started e2scrub_all.timeretadata Check for All Filesystems.
[  OK  ] Started fstrim.timer - Discard unused blocks once a week.
[  OK  ] Started fwupd-refresh.time… Refresh fwupd metadata regularly.
[  OK  ] Started man-db.timer - Daily man-db regeneration.
[  OK  ] Started systemd-tmpfiles-c… Cleanup of Temporary Directories.
[  OK  ] Reached target paths.target - Path Units.
[  OK  ] Reached target timers.target - Timer Units.
[  OK  ] Listening on avahi-daemon.…NS/DNS-SD Stack Activation Socket.
[  OK  ] Listening on cups.socket - CUPS Scheduler.
[  OK  ] Listening on dbus.socket- D-Bus System Message Bus Socket.
[  OK  ] Listening on ssh.socketpenBSD Secure Shell server socket.
[  OK  ] Reached target sockets.target - Socket Units.
[  OK  ] Reached target basic.target - Basic System.
         Starting accounts-daemon.service - Accounts Service...
         Starting avahi-daemon.serv…e - Avahi mDNS/DNS-SD Stack...
         Starting blueman-mechanism… Bluetooth management mechanism...
         Starting dbus.service - D-Bus System Message Bus...
         Starting e2scrub_reap.serv…e ext4 Metadata Check Snapshots...
[  OK  ] Started rknpu2.service - rknn server daemon.
         Starting rockchip-chromium…vironment for Rockchip Chromium...
[  OK  ] Started rsetup.service - rsetup configuration service.
         Starting sysfsutils.servic… variables from /etc/sysfs.conf...
         Starting systemd-logind.se…ice - User Login Management...
         Starting udisks2.service - Disk Manager...
         Starting upower.service - Daemon for power management...
         Starting zramswap.service - Linux zramswap setup...
[  OK  ] Started dbus.service - D-Bus System Message Bus.
[  OK  ] Found device dev-ttyS2.device - /dev/ttyS2.
[  OK  ] Finished rockchip-chromium…environment for Rockchip Chromium.
[FAILED] Failed to start zramswap.service - Linux zramswap setup.
See 'systemctl status zramswap.service' for details.
[  OK  ] Started avahi-daemon.service - Avahi mDNS/DNS-SD Stack.
[  OK  ] Finished networking.service - Raise network interfaces.
[  OK  ] Finished e2scrub_reap.serv…ine ext4 Metadata Check Snapshots.
[  OK  ] Started systemd-logind.service - User Login Management.
         Mounting boot-efi.mount - /boot/efi...
         Mounting config.mount - /config...
[  144.655001] FAT-fs (mmcblk0p2): Volume was not properly unmounted. Some data may be corrupt. Please run fsck.
         Starting NetworkManager.service - Network Manager...
[  144.681325] FAT-fs (mmcblk0p1): Volume was not properly unmounted. Some data may be corrupt. Please run fsck.
         Starting modprobe@drm.service - Load Kernel Module drm...
         Starting polkit.service - Authorization Manager...
         Starting wpa_supplicant.service - WPA supplicant...
[  OK  ] Started sysfsutils.service…fs variables from /etc/sysfs.conf.
[  OK  ] Mounted boot-efi.mount - /boot/efi.
[  OK  ] Mounted config.mount - /config.
[  OK  ] Finished modprobe@drm.service - Load Kernel Module drm.
[  OK  ] Started wpa_supplicant.service - WPA supplicant.
[  OK  ] Started polkit.service - Authorization Manager.
[  OK  ] Started accounts-daemon.service - Accounts Service.
[  OK  ] Started upower.service - Daemon for power management.
[  OK  ] Started NetworkManager.service - Network Manager.
[  OK  ] Reached target network.target - Network.
         Starting NetworkManager-wa…m - Network Manager Wait Online...
         Starting cups.service - CUPS Scheduler...
         Starting dnsmasq.serviceght DHCP and caching DNS server...
[  OK  ] Started radxa-adbd.service… adbd on supported Radxa products.
[  OK  ] Started radxa-usbnet.servi…ernet on supported Radxa products.
         Starting ssh.service - OpenBSD Secure Shell server...
         Starting systemd-user-sess…vice - Permit User Sessions...
[  OK  ] Started cups.service - CUPS Scheduler.
[  OK  ] Started udisks2.service - Disk Manager.
         Starting systemd-hostnamed.service - Hostname Service...
[  OK  ] Finished systemd-user-sess…ervice - Permit User Sessions.
         Starting lightdm.service - Light Display Manager...
         Starting plymouth-quit-wai… until boot process finishes up...
[  145.335429] file system registered
[  145.453737] read descriptors
[  145.454023] read strings
[  145.762538] rk_gmac-dwmac fe2a0000.ethernet end0: Register MEM_TYPE_PAGE_POOL RxQ-0
[  145.813952] rk_gmac-dwmac fe2a0000.ethernet end0: PHY [stmmac-0:01] driver [RTL8211F Gigabit Ethernet] (irq=POLL)
[  145.826859] dwmac4: Master AXI performs any burst length
[  145.827375] rk_gmac-dwmac fe2a0000.ethernet end0: No Safety Features support found
[  145.828631] rk_gmac-dwmac fe2a0000.ethernet end0: IEEE 1588-2008 Advanced Timestamp supported
[  145.829800] rk_gmac-dwmac fe2a0000.ethernet end0: registered PTP clock
[  145.830399] rk_gmac-dwmac fe2a0000.ethernet end0: configuring for phy/rgmii-id link mode
[  145.889687] rk_gmac-dwmac fe010000.ethernet end1: Register MEM_TYPE_PAGE_POOL RxQ-0
[  145.918921] rk_gmac-dwmac fe010000.ethernet end1: PHY [stmmac-1:01] driver [RTL8211F Gigabit Ethernet] (irq=POLL)
[  145.930959] dwmac4: Master AXI performs any burst length
[  145.931469] rk_gmac-dwmac fe010000.ethernet end1: No Safety Features support found
[  146.152382] rk_gmac-dwmac fe010000.ethernet end1: IEEE 1588-2008 Advanced Timestamp supported
[  146.154128] rk_gmac-dwmac fe010000.ethernet end1: registered PTP clock
[  146.154725] rk_gmac-dwmac fe010000.ethernet end1: configuring for phy/rgmii-id link mode
[  146.162948] mc: Linux media interface: v0.10
[  146.218409] videodev: Linux video capture interface: v2.00
[  146.219558] input: rk805 pwrkey as /devices/platform/fdd40000.i2c/i2c-0/0-0020/rk805-pwrkey.3.auto/input/input0
[  146.521337] Registered IR keymap rc-empty
[  146.521824] rc rc0: gpio_ir_recv as /devices/platform/ir-receiver/rc/rc0
[  146.522534] input: gpio_ir_recv as /devices/platform/ir-receiver/rc/rc0/input1
[  146.565895] rockchip-i2s-tdm fe410000.i2s: using zero-initialized flat cache, this may cause unexpected behavior
