# Method B payload — captured from working vendor (l1, conv2d-bsp.rknn) 2026-06-22

Tomeu's Method B step 1 DONE: the working rknn payload for our EXACT conv (conv2d 5x5 16→128,
input [1,80,80,16] INT8, output [1,40,40,128] INT8). Vendor computes it CORRECTLY.

## Golden result (the contrast Method B is built on)
- **Vendor (rknn, l1): out distinct=24, sum=26113009** — a REAL feature map (input = ramp (i*7)&0xff).
- **rocket (l3): out distinct=2, sum=26214400** — zero-point rail (degenerate).
- SAME model, SAME conv, opposite result → isolates to driver/submission, not the model.

## Payload components
- **cmdstream**: `conv2d-bsp-cmdstream.txt` (133 entries, REGCMD_FULL_BSP, real vendor IOVAs).
  regcmd_iova=0xfffeca00 (high). Head-arm entries [0,1,2,4,5] = 0x1004/0x2004/0x3004/0x4004/0x5004=0xe.
  CBUF_CON0 [3] 0x1040=0x44 (SAME as mesa for this conv → not a divergence here).
  Address-bearing slots to PATCH for replay with our BO IOVAs:
    [27] off=0x1070 val=0xffee0000  = input feature addr
    [36] off=0x1110 val=0xfffe0000  = weight addr
    [69] off=0x4020 val=0xfff19000  = DPU output addr
    [119] off=0x5020 val=0xfffec800 = RDMA addr
- **golden output**: `conv2d-golden-out.bin` (204800 B).
- **weights**: conv2d-bsp.rknn weight blob @ file-offset 0x1bc0 (NVDLA-packed, 51200 B). (Or dump the
  runtime weight BO at IOVA 0xfffe0000 via /dev/mem PA for byte-exact runtime copy.)
- **input**: ramp byte[i] = (i*7)&0xff, 102400 B (pass_through INT8).

## Runner used
`rknn_raw_run.c` (local) — minimal rknn_api runner, raw INT8 pass_through input, prints out
distinct/sum, writes golden output. Built on board:
  gcc rknn_raw_run.c -o rknn_raw_run -I<RK3588 api include> -L<aarch64 lib> -l:librknnrt.so -Wl,-rpath,<lib>

## UABIs for the dual-driver submitter (step 3/4)
- ROCKET (mainline, /dev/accel/accel0): DRM_IOCTL_ROCKET_{CREATE_BO,PREP_BO,FINI_BO,SUBMIT}.
  create_bo{size→handle,dma_address,offset}; submit→job{tasks[],in/out_bo_handles}; task{regcmd iova,
  regcmd_count}.
- RKNN (vendor, /dev/dri/card_minor): DRM_IOCTL_RKNPU_{ACTION,SUBMIT,MEM_CREATE,MEM_MAP}.
  mem_create{handle,flags,size,obj_addr,dma_addr,sram_size,core_mask}; submit{task_start,task_number,
  task_obj_addr,task_base_addr,core_mask,subcore_task[5]}; rknpu_task{regcmd_addr,regcfg_amount,
  enable_mask,int_mask,int_clear,...}.

## Next (step 2-4)
Build the submitter (run under rknn[l1]→verify golden, then rocket[l3]→does it engage or stall 0xf000).
Note: rocket replay needs BOs at IOVAs matching the cmdstream OR patch the 4 address slots above to our
allocated BO IOVAs (rocket create_bo returns dma_address). Board WiFi is FLAKY (drops ~1-2 min) — keep
board commands short.
