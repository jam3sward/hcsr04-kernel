# Using the Range Finder #

When the kernel module is loaded, it appears on the file system here:
```
/sys/kernel/hcsr04/range
```

Taking a range measurement is as simple as reading this file, for example:
```
cat /sys/kernel/hcsr04/range
26 157 1
```

The values displayed are:
  * Range in millimetres
  * Time period in microseconds
  * Status code (1 or 0)

In the example above, this means that the range is 26mm, the time period was 157us and the 1 indicates that the range finding operation was successful.