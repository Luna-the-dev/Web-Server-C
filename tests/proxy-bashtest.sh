#!/bin/bash

time curl http://localhost:3000/largefile1 & \
time curl http://localhost:3000/largefile2 & \
time curl http://localhost:3000/largefile3 & \
time curl http://localhost:3000/largefile4 & \
time curl http://localhost:3000/largefile5 & \
time curl http://localhost:3000/largefile6 & \
time curl http://localhost:3000/largefile7 & \
time curl http://localhost:3000/largefile8