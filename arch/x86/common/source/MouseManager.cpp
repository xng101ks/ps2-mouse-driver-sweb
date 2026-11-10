#include "MouseManager.h"
#include "Console.h"
#include "ArchCommon.h"
#include "Terminal.h"
#include "ports.h"
#include "debug.h"
#include "assert.h"

//these are some global variables we have to remember between interrupts
MouseManager *MouseManager::instance_ = 0;
unsigned int MouseManager::absolute_xpos = 0;
unsigned int MouseManager::absolute_ypos = 0;
uint8 MouseManager::current_readcycle = 0;
uint8 MouseManager::packet[3];
uint8 MouseManager::x_neg = 0;
uint8 MouseManager::y_neg = 0;
char MouseManager::remembered_char = 0;

//this function basically waits until we can send or receive information from the mouse again
void MouseManager::mouse_wait(uint8 dataorsignal)
{
  uint32 timeout = 1000000;
  if(dataorsignal == 0)
  {
    //we wait for data on port 0x60 by checking 0x64 (Bit 0)
    while(timeout > 0)
    {
      //check if there is data to read on port 0x60 of the PS2 controller
      //this is done by checking on port 0x64 if bit 0 is set
      if((inportb(0x64) & 1) == 1)
      {
        return;
      }
      //if not, decrement timer and try again
      timeout--;
    }
    debug(MOUSE_DRIVER, "waiting for mouse packet timed out\n");
  }
  else
  {
    //we are waiting to send a signal to port 0x60
    while(timeout > 0)
    {
      //before we can send stuff to 0x60 we need to check if the controller is ready for our data
      //this is done by checking port on 0x60 if Bit 1 is cleared
      if((inportb(0x64) & 2) == 0)
      {
        return;
      }
      //if not, decrement timer and try again
      timeout--;
    }
    debug(MOUSE_DRIVER, "waiting to send mouse packet timed out\n");
  }
}

MouseManager::MouseManager()
{
  debug(MOUSE_DRIVER, "setting up the mouse...\n");
  uint8 retval;

  //send the "enable Auxiliary Device" command to port 0x64 to enable the mouse
  mouse_wait(1);
  outportb(0x64, 0xA8);
 
  //on some systems, the PS2 aux port might be disabled at boot, so there might not be any interrupts generated
  //in order to enable it, the compaq status byte needs to be manipulated.
  //first we get the current value by sending the "Get Compaq Status Byte" command
  mouse_wait(1);
  outportb(0x64, 0x20);
  //the next byte sent by the mouse is the Status Byte
  //we or this with 2 to tell the controller to enable IRQ12
  mouse_wait(0);
  retval = inportb(0x60);
  debug(MOUSE_DRIVER, "Status Byte was %x\n", retval);
  retval |= 2;
  debug(MOUSE_DRIVER, "after or-ing Bit 1 it is: %x\n", retval);
  //0x60 means "Set Compaq Status", so we tell the controller that we now want to SET this byte
  mouse_wait(1);
  outportb(0x64, 0x60);
  //finally pass the byte we retrieved, OR'd with 2 to the PS2 controller to ensure IRQ12 is triggered
  mouse_wait(1);
  outportb(0x60, retval);
  //configuring the actual interrupt is done in the enableMouse mathor in the ArchInterrupts

  retval = read_cmd();
  debug(MOUSE_DRIVER, "After setting status bit, retval was: %x\n", retval);

  //load generic default settings (Disables streaming, sets the packet rate to 100 per second, and resolution to 4 pixels per mm.) 
  //this is done by sending 0xF6. More settings can be found here: https://wiki.osdev.org/Mouse_Input
  //0xEA is the byte for "enable streaming mode" in case this needs to be used later
  send_cmd(0xF6);
  //we need to "read back" the return value to let the mouse now we have aknowledged it's output
  read_cmd();
 
  //Finally, enable the mouse by telling it to automatically generate packets (this is done by sending 0xF4)
  send_cmd(0xF4);
  //again, we read back just to confirm we have acknowledged
  read_cmd();
}

MouseManager::~MouseManager()
{
}

void MouseManager::send_cmd(uint8 cmd)
{    
    //wait until we can send output to the mouse
    mouse_wait(1);
    //tell the controller we are addressing the mouse
    outportb(0x64, 0xD4);
    //wait again
    mouse_wait(1);
    //finally send the actual byte to the mouse
    outportb(0x60, cmd);
}

uint8 MouseManager::read_cmd()
{
  //wait until we can read data from the mouse
  mouse_wait(0);
  //get 1 Byte from the mouse (via the PS2 controller) and return this byte
  return inportb(0x60);
}

void MouseManager::serviceIRQ(void)
{
  //everytime the interrupt is called we can read a new byte from the mouse
  //a complete packet consists of 3 byte, so three interrupts make up one "exchange"
  if(MouseManager::current_readcycle == 0)
  {
    //first, we read the first byte, which contains some status bits and stuff
    //details can be found here: https://wiki.osdev.org/Mouse_Input
    packet[0] = read_cmd();
    debug(MOUSE_DRIVER, "first interrupt, byte: %x\n", packet[0]);
    //check if overflow bits are NOT set and if the "always set bit" is indeed set, otherwise discard packet
    if((packet[0] & 0x8) == 0)
    {
      debug(MOUSE_DRIVER, "the always 1 bit was not 1, delete this packet\n");
      return;
    }
    if((packet[0] & 0x40) != 0)
    {
      debug(MOUSE_DRIVER, "the x overflow bit was set, this is suspicious, delete this packet\n");
      return;
    }
    if((packet[0] & 0x80) != 0)
    {
      debug(MOUSE_DRIVER, "the y overflow bit was set, this is suspicious, delete this packet\n");
      return;
    }

    //if bit 5 is set, this means the relative y value will be negative when we calculate the absolute mouse position
    if((packet[0] & 0x20) != 0)
    {
      debug(MOUSE_DRIVER, "the y value will be negative\n");
      y_neg = 1;
    }
    //if bit 4 is set, this means the relative x value will be negative when we calculate the absolute mouse position
    if((packet[0] & 0x10) != 0)
    {
      debug(MOUSE_DRIVER, "the x value will be negative\n");
      x_neg = 1;
    }

    //everything has worked, so next time the interrupt is called, we will read the second byte
    current_readcycle++;
  }
  else if(MouseManager::current_readcycle == 1)
  {
    //now we can read the second byte and wait for the third one
    packet[1] = read_cmd();
    debug(MOUSE_DRIVER, "second interrupt, byte: %x\n", packet[1]);
    current_readcycle++;
  }
  else
  {
    //only at this point we update the positions with the bytes we got earlier and reset the cycle
    //so the next interrupt that is called will be the "first" one
    packet[2] = read_cmd();
    debug(MOUSE_DRIVER, "third interrupt, byte: %x\n", packet[2]);

    //reset the character were the mouse pointer is currently at
    if (currentThread && currentThread->getTerminal())
    {
      currentThread->getTerminal()->setCharacter(absolute_ypos, absolute_xpos, remembered_char);
    }

    //finally calculate the absolute positions from the deltas on the x and y axis
    if(x_neg == 0)
    {
      if(absolute_xpos + packet[1] > currentThread->getTerminal()->getNumColumns() - 1)
      {
        absolute_xpos = currentThread->getTerminal()->getNumColumns() - 1;
      }
      else
      {
        absolute_xpos += packet[1];
      }
    }
    else
    {
      packet[1] = (~packet[1]) + 1; //twos compliment
      if(absolute_xpos < packet[1])
      {
        absolute_xpos = 0;
      }
      else
      {
        absolute_xpos -= packet[1];
      }
    }

    if(y_neg == 0)
    {
      if(absolute_ypos < packet[2])
      {
        absolute_ypos = 0;
      }
      else
      {
        absolute_ypos -= packet[2];
      }
    }
    else
    {
      packet[2] = (~packet[2]) + 1; //twos compliment
      if(absolute_ypos + packet[2] > currentThread->getTerminal()->getNumRows() - 1)
      {
        absolute_ypos = currentThread->getTerminal()->getNumRows() - 1;
      }
      else
      {
        absolute_ypos += packet[2];
      }
    }

    //set the # mouse pointer to this current location by saving the character that is there currently
    //and then overriding it with
    remembered_char = currentThread->getTerminal()->getCharacter(absolute_ypos, absolute_xpos);
    currentThread->getTerminal()->setCharacter(absolute_ypos, absolute_xpos, '^');

    //reset everything for the next cycles, so the next interrupt will be treated as the "first"
    current_readcycle = 0;
    packet[0] = 0;
    packet[1] = 0;
    packet[2] = 0;
    y_neg = 0;
    x_neg = 0;
    debug(MOUSE_DRIVER, "current mouse position after irq: %u/%u\n", absolute_xpos, absolute_ypos);
  }
}