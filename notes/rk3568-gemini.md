Voici un récapitulatif complet, structuré pour le développement, regroupant toutes les informations extraites du TRM du Rockchip RK3568 concernant le GPU et le NPU.

# **Documentation Technique : GPU & NPU (Rockchip RK3568)**

Ce document compile les informations nécessaires à l'intégration matérielle et au développement de drivers (Kernel 6.x+) pour les accélérateurs de calcul du RK3568.

## **1\. Vue d'ensemble des Accélérateurs**

### **GPU (Graphics Processing Unit)**

* **Modèle :** ARM Mali-G52 2EE (Architecture Bifrost).  
* **Capacités :** OpenGL ES 1.1/2.0/3.2, Vulkan 1.1, OpenCL 2.0.  
* **Gestion Mémoire :** MMU interne dédiée.

### **NPU (Neural Processing Unit)**

* **Performance :** 1.0 TOPS.  
* **Précision :** INT8 / INT16.  
* **Support Modèles :** TensorFlow Lite, ONNX, PyTorch via conversion RKNPU.

## ---

**2\. Cartographie Mémoire et Interruptions**

| Composant | Adresse de Base | Taille | IRQ (GIC SPI) | Description |
| :---- | :---- | :---- | :---- | :---- |
| **GPU** | 0xfde60000 | 128 KB | 148 | Registres de contrôle Mali |
| **NPU** | 0xfe400000 | 1 MB | 142 | Registres de calcul NPU |
| **NPU IOMMU** | 0xfe440000 | 64 KB | \- | Unité de gestion mémoire NPU |
| **CRU** | 0xfdd10000 | 64 KB | \- | Clock & Reset Unit |
| **PMU** | 0xfdd90000 | 64 KB | \- | Power Management Unit |
| **SYS\_GRF** | 0xfdc60000 | 64 KB | \- | General Register Files |

## ---

**3\. Gestion de l'Horloge (CRU)**

Les horloges sont pilotées par le **CRU** via des PLL dédiées.

* **Sources principales :**  
  * **GPLL / CPLL :** Souvent utilisées pour le GPU.  
  * **NPLL :** Spécifiquement optimisé pour le NPU.  
* **Registres Clés :**  
  * **GPU :** CRU\_CLKSEL\_CON58 et CRU\_CLKSEL\_CON59 (Sélection source et diviseur pour clk\_gpu).  
  * **NPU :** CRU\_CLKSEL\_CON71 (Configuration de clk\_npu).  
  * **Gating :** \* CRU\_CLKGATE\_CON13 : Contrôle de l'horloge GPU.  
    * CRU\_CLKGATE\_CON31 : Contrôle des horloges clk\_npu, aclk\_npu (AXI) et hclk\_npu (AHB).

## ---

**4\. Domaines de Puissance et Tensions**

### **Domaines de Puissance (Power Domains)**

Gérés par le **PMU**, ils permettent de couper l'alimentation logique pour économiser l'énergie.

| Domaine | Registre (PMU\_PWR\_GATE\_SFTCON) | Statut (PMU\_PWR\_GATE\_ST) |
| :---- | :---- | :---- |
| **PD\_GPU** | Bit 0 (pd\_gpu\_dwn\_ena) | Bit 0 (pd\_gpu\_dwn\_stat) |
| **PD\_NPU** | Bit 1 (pd\_npu\_dwn\_ena) | Bit 1 (pd\_npu\_dwn\_stat) |

### **Rails de Tension (Voltage Domains)**

Alimentés par le PMIC externe, essentiels pour le DVFS (Dynamic Voltage and Frequency Scaling).

* **VDD\_GPU :** Rail dédié au GPU.  
* **VDD\_NPU :** Rail dédié au NPU (parfois partagé selon le design PCB).

## ---

**5\. Registres Critiques pour le Driver**

### **Contrôle PMU (Offset Base 0xfdd90000)**

* **PMU\_PWR\_GATE\_SFTCON (0x00A0) :** \* *Note :* Écrire les bits 0-1 pour le contrôle, et les bits 16-17 pour le "Write Enable".  
* **PMU\_PWR\_GATE\_ST (0x0098) :** Lecture seule pour confirmer l'état (0 \= On, 1 \= Off).

### **Reset Signaux (CRU Base 0xfdd10000)**

Utilisez ces bits pour réinitialiser les IPs avant initialisation :

* **SRST\_GPU\_A :** CRU\_SOFTRST\_CON13, bit 0\.  
* **SRST\_NPU\_A / H / P :** Situés dans les registres de reset spécifiques au NPU (généralement CRU\_SOFTRST\_CONxx).

## ---

**6\. Exemple de Configuration Device Tree (Kernel 6.19)**

Extrait de code

/\* Définition simplifiée pour DTSI \*/

gpu: gpu@fde60000 {  
    compatible \= "rockchip,rk3568-mali", "arm,mali-bifrost";  
    reg \= \<0x0 0xfde60000 0x0 0x20000\>;  
    interrupts \= \<GIC\_SPI 148 IRQ\_TYPE\_LEVEL\_HIGH\>;  
    interrupt-names \= "job", "mmu", "gpu";  
    clocks \= \<\&cru CLK\_GPU\>;  
    power-domains \= \<\&pmu RK3568\_PD\_GPU\>;  
    operating-points-v2 \= \<\&gpu\_opp\_table\>;  
    status \= "disabled";  
};

npu: npu@fe400000 {  
    compatible \= "rockchip,rk3568-npu";  
    reg \= \<0x0 0xfe400000 0x0 0x100000\>;  
    interrupts \= \<GIC\_SPI 142 IRQ\_TYPE\_LEVEL\_HIGH\>;  
    clocks \= \<\&cru CLK\_NPU\>, \<\&cru ACLK\_NPU\>, \<\&cru HCLK\_NPU\>;  
    clock-names \= "npu", "axi", "ahb";  
    resets \= \<\&cru SRST\_NPU\_A\>, \<\&cru SRST\_NPU\_H\>, \<\&cru SRST\_NPU\_P\>;  
    reset-names \= "axi", "ahb", "pclk";  
    power-domains \= \<\&pmu RK3568\_PD\_NPU\>;  
    iommus \= \<\&npu\_iommu\>;  
    status \= "disabled";  
};

## **7\. Recommandations Driver**

1. **Séquençage :** Toujours activer le domaine de puissance (PD) avant d'activer les horloges (CLK).  
2. **IOMMU :** Le NPU nécessite que son IOMMU soit attaché au groupe DMA dès l'initialisation pour éviter les fautes de bus AXI.  
3. **DVFS :** Pour le Kernel 6.19, privilégiez l'utilisation du framework devfreq pour coupler les fréquences GPU/NPU aux régulateurs VDD\_GPU/NPU.