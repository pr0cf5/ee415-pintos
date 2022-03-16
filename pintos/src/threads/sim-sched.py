#!/usr/bin/python3
class Thread:
    def __init__(self, name, nice, pri=63):
        self.name = name
        self.nice = nice
        self.pri = pri
        self.recent_cpu = 0

    def recalc_priority(self):
        self.pri = 63 - (self.recent_cpu/4) - (self.nice*2)

class Kernel:
    def __init__(self, initial_thread):
        self.ready_list = []
        self.current = initial_thread
        self.load_avg = 0
        self.ticks = 0

    def add_thread(self, th):
        self.ready_list.append(th)

    def tick(self):
        next_th = max(self.ready_list, key=lambda t: t.pri)
        if next_th.pri > self.current.pri:
            self.ready_list.remove(next_th)
            self.ready_list.append(self.current)
            self.current = next_th
        if self.ticks % 4 == 0:
            self.current.recalc_priority()
            for t in self.ready_list:
                t.recalc_priority()
        self.current.recent_cpu += 1
        self.ticks += 1

if __name__ == "__main__":
    thA = Thread("threadA", 0)
    thB = Thread("threadB", 1)
    thC = Thread("threadC", 2)
    k = Kernel(thA)
    k.add_thread(thB)
    k.add_thread(thC)
    for i in range(100):
        k.tick()
        if i % 4 == 0:
            output_str = "%2d   %4d%4d%4d%4d%4d%4d"%(k.ticks-1, thA.recent_cpu, thB.recent_cpu, thC.recent_cpu, thA.pri, thB.pri, thC.pri)
            print(output_str)
        