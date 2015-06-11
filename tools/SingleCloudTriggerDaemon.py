#!/usr/bin/python

# This is a quick script manually adjusted for my acquisitions

import os, sys, glob, time
import pygame
from pygame.locals import *

defaultJoystickID = 0

buttonX = 0
buttonA = 1
buttonB = 2
buttonY = 3
buttonLB = 4
buttonRB = 5
buttonLT = 6
buttonRT = 7
buttonBACK = 8
buttonSTART = 9
buttonLeftAxis = 10
buttonRightAxis = 11

pygame.init()
if pygame.joystick.get_count() == 0:
  print ("Error, I did not find any joysticks")
else:
  joystick = pygame.joystick.Joystick(defaultJoystickID)
  joystick.init()

commandPrefix = "nohup " # Prevent program hang-up
commandSuffix = " > /dev/null 2>&1&" # Prevent all command output
timeForAScan = 27;
while True:
  pygame.event.wait() # Prevent from using all resources
  pygame.event.get()

  if joystick.get_button(buttonA):
    os.system(commandPrefix + "roslaunch ptu_laser_assembler singleAcquisitionHighDensity.launch" + commandSuffix);
    time.sleep(timeForAScan)
  elif joystick.get_button(buttonB):
    pygame.quit()
    sys.exit()
