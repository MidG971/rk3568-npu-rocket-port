#!/usr/bin/env python3
# Add `vendor_power` param: match rknpu_power_on's raw clk_set_rate two-step (600MHz
# intermediate on scmi_clk = clks[0]) instead of rocket's opp_set_rate(200M) two-step.
# vdd set to target once via OPP (RK3568 NPU has no sram-supply, so no mem regulator).
import sys, time, shutil
P="/home/radxa/npu-port/linux-mainline/drivers/accel/rocket/rocket_drv.c"
s=open(P).read()
if "VENDOR_POWER" in s:
    print("ALREADY PATCHED"); sys.exit(0)

# param near compute_hz
pa="long compute_hz = 1000000000L;\nmodule_param(compute_hz, long, 0644);"
if s.count(pa)!=1: print("compute_hz anchor count=%d abort"%s.count(pa)); sys.exit(1)
s=s.replace(pa, pa+"\nstatic int vendor_power;\nmodule_param(vendor_power, int, 0644);\nMODULE_PARM_DESC(vendor_power, \"Vendor-faithful power-on: raw clk_set_rate 600M two-step on scmi_clk (1=on)\");",1)

# wrap the opp two-step block
old='''	{
		/* two-step: 200MHz intermediate (<=400, no PVTPLL) defeats the
		 * clock-framework unchanged-rate fast-path, then the target. */
		int ret = dev_pm_opp_set_rate(dev, 200000000UL);
		if (ret)
			dev_warn(dev, "rocket: opp_set_rate(200M) ret=%d\\n", ret);
		ret = dev_pm_opp_set_rate(dev, (unsigned long)compute_hz);
		if (ret)
			dev_warn(dev, "rocket: opp_set_rate(%ld) ret=%d\\n", compute_hz, ret);
		else
			dev_info(dev, "rocket: opp_set_rate(%ldHz) OK\\n", compute_hz);
	}'''
if s.count(old)!=1: print("opp two-step anchor count=%d abort"%s.count(old)); sys.exit(1)
new='''	if (vendor_power) {
		/* VENDOR_POWER: match rknpu_power_on exactly — set vdd to target via
		 * OPP once (no voltage bounce), then a RAW clk_set_rate two-step on
		 * scmi_clk (clks[0]) with a 600MHz intermediate to force TF-A
		 * clk_npu_set_rate()/PVTPLL re-init. rknpu uses clk-only rate changes. */
		struct clk *scmi = rdev->cores[core].clks[0].clk;
		int ret = dev_pm_opp_set_rate(dev, (unsigned long)compute_hz);
		if (ret)
			dev_warn(dev, "rocket: VENDOR_POWER opp(target) ret=%d\\n", ret);
		clk_set_rate(scmi, 600000000UL);
		clk_set_rate(scmi, (unsigned long)compute_hz);
		dev_info(dev, "rocket: VENDOR_POWER clk two-step 600M->%ldHz rate=%lu\\n",
			 compute_hz, clk_get_rate(scmi));
	} else {
		/* two-step: 200MHz intermediate (<=400, no PVTPLL) defeats the
		 * clock-framework unchanged-rate fast-path, then the target. */
		int ret = dev_pm_opp_set_rate(dev, 200000000UL);
		if (ret)
			dev_warn(dev, "rocket: opp_set_rate(200M) ret=%d\\n", ret);
		ret = dev_pm_opp_set_rate(dev, (unsigned long)compute_hz);
		if (ret)
			dev_warn(dev, "rocket: opp_set_rate(%ld) ret=%d\\n", compute_hz, ret);
		else
			dev_info(dev, "rocket: opp_set_rate(%ldHz) OK\\n", compute_hz);
	}'''
s=s.replace(old,new,1)
shutil.copy(P, P+".bak-vendorpower-"+time.strftime("%Y%m%d-%H%M%S"))
open(P,"w").write(s)
print("PATCHED OK")
