#!/usr/bin/env python3
"""cuBLAS FP16 -> FP32-accumulate GEMM benchmark.

This is the metric that the gpu-unlock project reports: the artificially
rate-limited Tensor Core path doubles when the SS0/SS1 selectors are unlocked.

Usage:
    python fp16_gemm_bench.py [DLL_DIR] [SIZE]

    DLL_DIR  directory containing cudart64_*.dll and cublas64_*.dll
             (default: CUDA_PATH\\bin, or the directory of this script)
    SIZE     square matrix size (default 4096)

Notes:
    * 4096**3 with 50 iterations takes ~0.2 s on a 3060; larger sizes can trip the
      Windows WDDM TDR timeout (~2 s of continuous GPU work), so keep it small.
    * Expect roughly 24 TFLOPS locked and 38-45 TFLOPS unlocked on an RTX 3060
      12 GB (170 W power limit).
"""

import ctypes
import glob
import os
import sys


def find_dlls(hint):
    candidates = []
    if hint:
        candidates.append(hint)
    if os.environ.get("CUDA_PATH"):
        candidates.append(os.path.join(os.environ["CUDA_PATH"], "bin"))
    candidates.append(os.path.dirname(os.path.abspath(__file__)))
    candidates += glob.glob(r"C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\*\bin")
    for d in candidates:
        if not d or not os.path.isdir(d):
            continue
        cudart = glob.glob(os.path.join(d, "cudart64_*.dll"))
        cublas = glob.glob(os.path.join(d, "cublas64_*.dll"))
        if cudart and cublas:
            return d, cudart[0], cublas[0]
    raise SystemExit(
        "cudart64_*.dll / cublas64_*.dll not found.\n"
        "Pass the directory as the first argument, e.g.\n"
        r"  python fp16_gemm_bench.py U:\llama"
    )


DLL_DIR, CUDART_PATH, CUBLAS_PATH = find_dlls(sys.argv[1] if len(sys.argv) > 1 else None)
if hasattr(os, "add_dll_directory"):
    os.add_dll_directory(DLL_DIR)
cudart = ctypes.WinDLL(CUDART_PATH)   # noqa: E402
cublas = ctypes.WinDLL(CUBLAS_PATH)   # noqa: E402

# ---- cudart prototypes ------------------------------------------------------
cudart.cudaSetDevice.argtypes = [ctypes.c_int]
cudart.cudaMalloc.argtypes = [ctypes.POINTER(ctypes.c_void_p), ctypes.c_size_t]
cudart.cudaMemset.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_size_t]
cudart.cudaFree.argtypes = [ctypes.c_void_p]
cudart.cudaDeviceSynchronize.restype = ctypes.c_int
cudart.cudaEventCreate.argtypes = [ctypes.POINTER(ctypes.c_void_p)]
cudart.cudaEventRecord.argtypes = [ctypes.c_void_p, ctypes.c_void_p]
cudart.cudaEventElapsedTime.argtypes = [ctypes.POINTER(ctypes.c_float), ctypes.c_void_p, ctypes.c_void_p]
cudart.cudaEventDestroy.argtypes = [ctypes.c_void_p]
cudart.cudaGetErrorString.argtypes = [ctypes.c_int]
cudart.cudaGetErrorString.restype = ctypes.c_char_p

# ---- cublas prototypes ------------------------------------------------------
cublas.cublasCreate_v2.argtypes = [ctypes.POINTER(ctypes.c_void_p)]
cublas.cublasDestroy_v2.argtypes = [ctypes.c_void_p]
cublas.cublasSetMathMode.argtypes = [ctypes.c_void_p, ctypes.c_int]
cublas.cublasGemmEx.argtypes = [
    ctypes.c_void_p,                              # handle
    ctypes.c_int, ctypes.c_int,                   # transa, transb
    ctypes.c_int, ctypes.c_int, ctypes.c_int,     # m, n, k
    ctypes.c_void_p,                              # alpha
    ctypes.c_void_p, ctypes.c_int, ctypes.c_int,  # A, Atype, lda
    ctypes.c_void_p, ctypes.c_int, ctypes.c_int,  # B, Btype, ldb
    ctypes.c_void_p,                              # beta
    ctypes.c_void_p, ctypes.c_int, ctypes.c_int,  # C, Ctype, ldc
    ctypes.c_int,                                 # computeType
    ctypes.c_int,                                 # algo
]

CUDA_R_32F = 0
CUDA_R_16F = 2
CUBLAS_COMPUTE_32F = 68
CUBLAS_GEMM_DEFAULT_TENSOR_OP = 99
CUBLAS_OP_N = 0


def check(rc, what):
    if rc != 0:
        err = cudart.cudaGetErrorString(rc) if rc < 1000 else b""
        raise RuntimeError(f"{what} failed: rc={rc} {err}")


def run(m, k, n, label, iters=50, warmup=10):
    a = ctypes.c_void_p()
    b = ctypes.c_void_p()
    c = ctypes.c_void_p()
    handle = ctypes.c_void_p()
    e0 = ctypes.c_void_p()
    e1 = ctypes.c_void_p()
    try:
        check(cudart.cudaSetDevice(0), "cudaSetDevice")
        check(cudart.cudaMalloc(ctypes.byref(a), m * k * 2), "cudaMalloc A")
        check(cudart.cudaMalloc(ctypes.byref(b), k * n * 2), "cudaMalloc B")
        check(cudart.cudaMalloc(ctypes.byref(c), m * n * 4), "cudaMalloc C")
        check(cudart.cudaMemset(a, 0, m * k * 2), "memset A")
        check(cudart.cudaMemset(b, 0, k * n * 2), "memset B")
        check(cudart.cudaMemset(c, 0, m * n * 4), "memset C")
        check(cublas.cublasCreate_v2(ctypes.byref(handle)), "cublasCreate")
        cublas.cublasSetMathMode(handle, 1)
        alpha = ctypes.c_float(1.0)
        beta = ctypes.c_float(0.0)
        args = [handle, CUBLAS_OP_N, CUBLAS_OP_N, m, n, k,
                ctypes.byref(alpha),
                a, CUDA_R_16F, m,
                b, CUDA_R_16F, k,
                ctypes.byref(beta),
                c, CUDA_R_32F, m,
                CUBLAS_COMPUTE_32F, CUBLAS_GEMM_DEFAULT_TENSOR_OP]

        def once():
            rc = cublas.cublasGemmEx(*args)
            if rc != 0:
                raise RuntimeError(f"cublasGemmEx rc={rc}")
            check(cudart.cudaDeviceSynchronize(), "sync")

        for _ in range(warmup):
            once()
        check(cudart.cudaEventCreate(ctypes.byref(e0)), "event0")
        check(cudart.cudaEventCreate(ctypes.byref(e1)), "event1")
        check(cudart.cudaEventRecord(e0, None), "record0")
        for _ in range(iters):
            once()
        check(cudart.cudaEventRecord(e1, None), "record1")
        ms = ctypes.c_float(0.0)
        check(cudart.cudaEventElapsedTime(ctypes.byref(ms), e0, e1), "elapsed")
        tflops = 2.0 * m * n * k * iters / (ms.value / 1000.0) / 1e12
        print(f"{label:28s} {m}x{k}x{n}  {ms.value / iters:8.2f} ms/iter  {tflops:8.2f} TFLOPS")
        return tflops
    finally:
        for h in (a, b, c):
            if h.value:
                cudart.cudaFree(h)
        if handle.value:
            cublas.cublasDestroy_v2(handle)


def main():
    size = int(sys.argv[2]) if len(sys.argv) > 2 else 4096
    print(f"cuBLAS/cudart from: {DLL_DIR}")
    print("---")
    run(size, size, size, "FP16 in / FP32 accum (limited)")


if __name__ == "__main__":
    main()
