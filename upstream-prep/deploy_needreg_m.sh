#!/bin/bash
# Deploy the need_regulator + rocket=m test kernel as a NEW boot entry (l6).
# Does NOT touch l3/l4/l5 and does NOT change the default boot entry.
# Run ON the board: bash /mnt/nvme/build/deploy_needreg_m.sh
set -e
cd /mnt/nvme/build/npu-7.1
REL=$(make -s ARCH=arm64 kernelrelease)
echo "KERNELRELEASE=$REL"

# 1. Image + dtb under distinct names
sudo cp arch/arm64/boot/Image                                   /boot/Image-7.1-needreg-m
sudo cp arch/arm64/boot/dts/rockchip/rk3568-rock-3b.dtb         /boot/rk3568-rock-3b-7.1-needreg-m.dtb
ls -la /boot/Image-7.1-needreg-m /boot/rk3568-rock-3b-7.1-needreg-m.dtb

# 2. Install ONLY rocket.ko (no module deps: DRM core is =y) + depmod
KO=drivers/accel/rocket/rocket.ko
MD="/lib/modules/$REL/kernel/drivers/accel/rocket"
sudo mkdir -p "$MD"
sudo cp "$KO" "$MD/rocket.ko"
sudo depmod -a "$REL"
echo "--- modinfo (installed) ---"
modinfo "$MD/rocket.ko" | grep -E "vermagic|depends|alias" | head
echo "--- modules.alias for rknn/rocket (udev autoload) ---"
grep -iE "rknn|rocket" /lib/modules/$REL/modules.alias | head

# 3. Append a NEW l6 entry (idempotent); keep default l3
sudo cp /boot/extlinux/extlinux.conf /boot/extlinux/extlinux.conf.bak-$(date +%Y%m%d-%H%M%S)
INITRD=$(ls -1 /boot/initrd.img-7.1* 2>/dev/null | head -1)
if grep -q "label l6" /boot/extlinux/extlinux.conf; then
  echo "l6 already present, leaving as-is (check it points at needreg-m)"
else
  sudo tee -a /boot/extlinux/extlinux.conf >/dev/null <<EOF

label l6
	menu label Mainline 7.1-rc6 NPU need_regulator + rocket=m (test)
	linux /boot/Image-7.1-needreg-m
	${INITRD:+initrd $INITRD}
	fdt /boot/rk3568-rock-3b-7.1-needreg-m.dtb
	append root=UUID=d210e617-6a4c-4771-b955-ddd835a32d2b rw rootwait earlycon console=ttyFIQ0,1500000n8 console=ttyS2,1500000n8 clk_ignore_unused cma=128M kernel.panic=5 fw_devlink=permissive
EOF
fi
echo "=== default (must stay l3) ==="; grep -E "^default" /boot/extlinux/extlinux.conf
echo "=== l6 entry ==="; awk '/label l6/{p=1} p{print} /append/&&p{exit}' /boot/extlinux/extlinux.conf
echo DEPLOY_DONE
