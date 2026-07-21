Voici le récapitulatif technique complet structuré selon votre demande pour le développement d'un driver sous Kernel 6.19.

# RK3568 GPU & NPU Register Summary (Kernel 6.19 Driver)

## 1. Toplevel Overview

* **Architecture :** Le GPU et le NPU sont des maîtres (masters) sur le bus système interconnecté via des interfaces **AXI 128-bit**.
* **Connectivité :** Ils sont reliés au **BIU** (Bus Interface Unit) qui gère la cohérence avec la mémoire système (DDR).
* **Domaines :** - **Power :** `PD_GPU` et `PD_NPU` sont isolables.
* **Reset :** Gérés par le CRU, permettant des réinitialisations à chaud sans impacter le reste du SoC.



---

## 2. GPU (Mali-G52 2EE) Registers

### 2.1 MMIO base address

* **Physical Address :** `0xfde60000`
* **Size :** `0x20000` (128 KB)
* **Description :** Registres de contrôle de l'architecture ARM Bifrost (utilisés par le driver `Panfrost`).

### 2.2 Register map (Integration/Control)

*Note : Les registres internes du cœur Mali sont standards ARM, mais les registres de contrôle système sont dans le GRF.*

| Offset | Name | R/W | Description |
| --- | --- | --- | --- |
| `0x0000` | GPU_ID | R | Identification du matériel (Bifrost G52) |
| `0x0010` | GPU_IRQ_RAWSTAT | R | État brut des interruptions (Job, MMU, GPU) |
| `0x001C` | GPU_IRQ_MASK | R/W | Masque d'interruptions |

### 2.3 Clocks

* **clk_gpu_core :** Horloge principale. Source : CPLL ou GPLL.
* **Registres CRU associés :** - `CRU_CLKSEL_CON58` (Bits [15:8] pour le diviseur, [1:0] pour la source).
* `CRU_CLKSEL_CON59` (Diviseurs secondaires).



### 2.4 Interrupts

* **GIC IRQ ID :** `148` (SPI).
* **Type :** Level High.
* **Usage :** Job slot completion, MMU faults, GPU performance counters.

---

## 3. NPU Registers

### 3.1 MMIO base address

* **Physical Address :** `0xfe400000`
* **Size :** `0x100000` (1 MB)
* **NPU IOMMU :** `0xfe440000` (64 KB)

### 3.2 Register map (NPU IOMMU)

Indispensable pour le mapping DMA dans le kernel.

| Offset | Name | R/W | Reset | Description |
| --- | --- | --- | --- | --- |
| `0x0000` | DTE_ADDR | R/W | 0x0 | Directory Table Address (Page Table) |
| `0x0004` | STATUS | R | 0x0 | MMU Status (Fault, Paging active) |
| `0x0008` | COMMAND | W | 0x0 | ZAP_CACHE, MMU_ENABLE |

---

## 4. Clock & Reset Control (CRU)

Base : `0xfdd10000`

* **NPU Clocks :**
* `clk_npu` (Core) : `CRU_CLKSEL_CON71`. Source privilégiée : **NPLL**.
* `aclk_npu` (Bus) : `CRU_CLKSEL_CON70`.
* `hclk_npu` (AHB) : `CRU_CLKSEL_CON70`.


* **Soft Resets :**
* **GPU :** `CRU_SOFTRST_CON13`, Bit 0 (`srst_gpu_a`).
* **NPU :** `CRU_SOFTRST_CON22`, Bits 0-2 (`srst_npu_a`, `srst_npu_h`, `srst_npu_p`).



---

## 5. Power Domains

Base PMU : `0xfdd90000`

* **PD_GPU :**
* Activation : `PMU_PWR_GATE_SFTCON` (Offset `0x00A0`), Bit 0.
* Status : `PMU_PWR_GATE_ST` (Offset `0x0098`), Bit 0.


* **PD_NPU :**
* Activation : `PMU_PWR_GATE_SFTCON` (Offset `0x00A0`), Bit 1.
* Status : `PMU_PWR_GATE_ST` (Offset `0x0098`), Bit 1.



*Logique Driver :* Pour Kernel 6.19, utilisez `genpd` (Generic Power Domains) pour lier ces bits au runtime PM.

---

## 6. IOMMU / DMA

* **GPU MMU :** Interne au Mali G52. Gérée via les registres de l'espace `0xfde60000`.
* **NPU IOMMU :**
* Requis pour le partage de buffers (`dmabuf`) avec le VPU ou l'ISP.
* Le driver doit configurer le registre `SYS_GRF_SOC_CONx` pour autoriser le passage du trafic NPU à travers l'IOMMU dédié.


* **QoS (Quality of Service) :**
* Des registres dans le **GRF** (`0xfdc60000`) permettent de définir la priorité AXI du GPU/NPU par rapport au CPU. Valeur recommandée pour le NPU lors d'inférence temps réel : `Priority 2` ou `3`.
