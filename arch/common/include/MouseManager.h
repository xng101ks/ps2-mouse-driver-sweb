#pragma once

#include "types.h"

#ifdef __cplusplus

extern "C"
{
#endif

#ifdef __cplusplus
}
#endif

class MouseManager
{
  public:
    MouseManager();
    ~MouseManager();
    static MouseManager *instance()
    {
      if (!instance_)
        instance_ = new MouseManager();
      return instance_;
    }

    /**
     * writes a byte to the given IO port
     *
     */
    void send_cmd(uint8 cmd);

    uint8 read_cmd();

    void serviceIRQ(void);

    void mouse_wait(uint8 dataorsignal);

    static unsigned int absolute_xpos;
    static unsigned int absolute_ypos;
    static unsigned int max_xpos;
    static unsigned int max_ypos;
    static uint8 current_readcycle;
    static uint8 packet[3];
    static uint8 x_neg;
    static uint8 y_neg;
    static char remembered_char;

  protected:

    static MouseManager *instance_;
};
