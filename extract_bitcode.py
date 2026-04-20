import os
import glob
import subprocess

def dump_bc(base, dis, count, data):
    filename = "{}/seg{}.bc".format(base, count)
    with open(filename, "wb") as f:
        f.write(data)
    subprocess.run([dis, "seg{}.bc".format(count)], cwd=base)
    print("Dumped: {}".format(filename))

def extract_bc(filename: str, objcopy, dis):
    bc_header = bytes([0x42, 0x43, 0xc0, 0xde])
    basedir = filename+"_bc"
    bc_filename = filename+".bc"

    subprocess.run([objcopy, filename, "--dump-section",
                   ".llvmbc={}".format(bc_filename)])

    if not os.path.exists(bc_filename):
        print("Not found: {}".format(bc_filename))
        exit(1)
        return

    os.makedirs(basedir, exist_ok=True)

    count = 0

    with open(bc_filename, 'rb') as f:
        data = f.read()

        last_pos = 0
        while True:
            pos = data.find(bc_header, last_pos+4)
            if pos == -1:
                dump_bc(basedir, dis, count, data[last_pos:])
                break
            else:
                dump_bc(basedir, dis, count, data[last_pos: pos])
            last_pos = pos
            count += 1

def main():
    if os.path.exists("final.ll"):
        os.remove("final.ll")

    ### Change the filename to the binary file you want to extract
    filename = "xxxx"
   
    objcopy = "llvm-objcopy"
    dis = "llvm-dis"
    
    extract_bc(filename, objcopy, dis)
    
    ll_files = glob.glob(filename + "_bc/*.ll")
    subprocess.run(["llvm-link"] + ll_files + ["-S", "-o", "final.ll"])
    
    if (os.path.exists("final.ll")):
        print("Linked: final.ll")
    else:
        print("Failed to link")



if __name__ == "__main__":
    main()
