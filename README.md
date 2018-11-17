# vsFat - Virtual Synthetic filesystem

This program generates a synthetic FAT32 filesystem given a directory on the 
local system. The primary purpose of such an arrangement is to export the FS
via usb-host mode to a physical device that is incapable of connecting to
a network drive.

This FS is read-only, limited to 2TB of total size and 4GB maximum file sizes,
as per the FAT32 specification.

## Running

vsFat is ready to go out of the box. Download, compile with make and execute 
the following :

    sudo modprobe nbd
    sudo ./vsfat /dev/nbd0 /path/to/export &
    
To automatically load nbd at boot, execute the following:

    echo "nbd" | sudo tee -a /etc/modules
    
Then, vsfat can be launched automatically from rc.local or systemd, followed by the
usb device module.
    
It will process for a few seconds while it catalogs all of the files under the
given path. When complete, it will say:

    Scan complete, launching block device
    
Once this is done, /dev/nbd0 will appear as a 2TB HDD, with a single 2TB 
partition formatted FAT32 containing all the files inside /path/to/export

## Sizing
The size of the synthetic disk depends upon two settings in setup.h:
    
    Fat32_Sectors_per_Cluster
    FAT32_FAT_Table_Length
    
Ideally, adjust the size only with the Sectors_per_Cluster. 
The following are possible values and the resulting disk size:

    SecPerClus	Disk Size (G)	Fat Size (Mb)
    1	        0.5	            4
    2       	2	            8
    4	        8	            16
    8	        32	            32
    16	        128	            64
    32	        512	            128
    64	        2048	        256

If this isn't fine grain enough for your application, you can also adjust the FAT_Table_Length, which will also adjust the final size proportionally to the adjustment made. Reducing this value by 50% will shrink the disk to 50% of the above size.

Note that even though the disk is virtual, the filesystem elements must be generated and kept in RAM. Larger sized disks require more space for the filesystem elements, as noted in the Fat Size column of the above table. Additionally, RAM is required for keeping track of filenames and the memory map of the system. Both the filenames and memory map grow proportionally with the number of files on the hosted filesystem. In short, use the smallest disk size you can in order to save RAM for the filenames and memory mapping.

## USB Device Mode

Thanks to By Andrew Mulholland (gbaman) and his Gist at 
https://gist.github.com/gbaman/50b6cca61dd1c3f88f41 for clean, consise instructions 
on setting this up. Pertinent sections reproduced here.  

### 2. Modular, but slower to setup method
 
    
1. First, flash Jessie (only tested on full, lite version may also work though) onto a blank microSD card.  
2. **(step only needed if running Raspbain version before 2016-05-10)** Once it starts up again, run ```sudo BRANCH=next rpi-update```. This will take a while.  
3. Next we need to make sure we are using the dwc2 USB driver ```echo "dtoverlay=dwc2" | sudo tee -a /boot/config.txt```.
4. And enable it in Raspbian ```echo "dwc2" | sudo tee -a /etc/modules```
5. Do not load the device module at boot by echoing to /etc/modules. It needs to be run AFTER vsFat is running.
    
### Using the modules

- **g_mass_storage** - To have your Pi Zero appear as a mass storage device (flash drive), connected to the nbd0 device for example ```sudo modprobe g_mass_storage  file=/dev/nbd0 stall=0```.

In theory, most USB devices should work alongside these kernels, to switch to USB OTG mode, simply don't use an OTG adapter cable and use a standard USB cable to plug your Pi Zero into another computer, it should auto switch.   

## Based on BUSE - A block device in userspace

This piece of software was inspired by FUSE, which allows the development of
Linux file systems that run in userspace. The goal of BUSE is to allow virtual
block devices to run in userspace as well. Currently BUSE is experimental and
should not be used for production code.

Implementing a block device with BUSE is fairly straightforward. Simply fill
`struct buse_operations` (declared in `buse.h`) with function pointers that
define the behavior of the block device, and set the size field to be the
desired size of the device in bytes. Then call `buse_main` and pass it a
pointer to this struct. `busexmp.c` is a simple example example that shows how
this is done.

The implementation of BUSE itself relies on NBD, the Linux network block device,
which allows a remote machine to serve requests for reads and writes to a
virtual block device on the local machine. BUSE sets up an NBD server and client
on the same machine, with the server executing the code defined by the BUSE
user.

## Running the Example Code

BUSE comes with an example driver in `busexmp.c` that implements a 128 MB
memory disk. To try out the example code, run `make` and then execute the
following as root:

    modprobe nbd
    ./busexmp /dev/nbd0

You should then have an in-memory disk running, represented by the device file
`/dev/nbd0`. You can create a file system on the virtual disk, mount it, and
start reading and writing files on it:

    mkfs.ext4 /dev/nbd0
    mount /dev/nbd0 /mnt
