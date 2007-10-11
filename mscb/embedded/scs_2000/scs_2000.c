/********************************************************************\

  Name:         scs_2000.c
  Created by:   Stefan Ritt


  Contents:     General-purpose framework for SCS-2000 control unit.

  $Id$

\********************************************************************/

#include <stdio.h>
#include <string.h>
#include <math.h>
#include <intrins.h>
#include "mscbemb.h"
#include "scs_2000.h"

extern bit FREEZE_MODE;
extern bit DEBUG_MODE;

char code node_name[] = "SCS-2000";
char code svn_rev_2000[] = "$Rev$";
extern char code svn_rev_main[];
extern char code svn_rev_lib[];

/* declare number of sub-addresses to framework */
unsigned char idata _n_sub_addr = 1;

extern void lcd_menu(void);

/*---- Port definitions ----*/

bit b0, b1, b2, b3, master;

/*---- Define variable parameters returned to CMD_GET_INFO command ----*/

#define N_PORT 16 /* maximal one master and one slave module */

/* data buffer (mirrored in EEPROM) */

MSCB_INFO_VAR *variables;
float xdata user_data[N_PORT*8];
float xdata backup_data[N_PORT*8];

/********************************************************************\

  Application specific init and inout/output routines

\********************************************************************/

MSCB_INFO_VAR xdata vars[N_PORT*8+1];

unsigned char xdata update_data[N_PORT*8];
unsigned char xdata erase_module;

unsigned char xdata module_nvars[N_PORT];
unsigned char xdata module_id[N_PORT];
unsigned char xdata module_index[N_PORT];

unsigned char xdata memsize;
extern SCS_2000_MODULE code scs_2000_module[];
extern unsigned char idata n_variables;

void user_write(unsigned char index) reentrant;
void scan_modules(void);

/*---- User init function ------------------------------------------*/

extern SYS_INFO sys_info;

void user_init(unsigned char init)
{
   unsigned char xdata i, j, index, var_index;
   char xdata str[6];

   erase_module = 0xFF;

   /* red (upper) LED off by default */
   led_mode(1, 0);

   /* issue an initial reset */
   for (i=0 ; i<N_PORT/8 ; i++)
      power_mgmt(i, 1);

   /* check if master or slave */
   master = is_master();

   if (master) {
      /* initial EEPROM value */
      if (init) {
         var_index = 0;
         for (i=0 ; i<N_PORT ; i++) {
            index = module_index[i];
            if (index != 0xFF && scs_2000_module[index].driver) {
               for (j=0 ; j<module_nvars[i] ; j++)
                  /* default variables to zero */
                  memset(variables[var_index].ud, 0, variables[var_index].width);
                  /* allow driver to overwrite */
                  scs_2000_module[index].driver(module_id[i], MC_GETDEFAULT, i/8, i%8, j, 
                     variables[var_index++].ud);
                  var_index++;
            } 
         }
      } else {
         /* retrieve backup data from RAM if not reset by power on */
         SFRPAGE = LEGACY_PAGE;
         if ((RSTSRC & 0x02) == 0)
            memcpy(&user_data, &backup_data, sizeof(user_data));
      }
   
      /* initialize drivers */
      for (i=0 ; i<N_PORT ; i++)
         if (module_index[i] != 0xFF && scs_2000_module[module_index[i]].driver)
            scs_2000_module[module_index[i]].driver(module_id[i], MC_INIT, i/8, i%8, 0, NULL);
   
      /* write digital outputs */
      for (i=0 ; i<n_variables ; i++)
         user_write(i);
   
      /* write remote variables */
      for (i = 0; variables[i].width; i++)
         if (variables[i].flags & MSCBF_REMOUT)
            send_remote_var(i);
   }

   /* display startup screen */
   lcd_goto(0, 0);
   for (i=0 ; i<7-strlen(sys_info.node_name)/2 ; i++)
      puts(" ");
   puts("** ");
   puts(sys_info.node_name);
   puts(" ** ");
   lcd_goto(0, 1);
   printf("   Address:  %04X", sys_info.node_addr);
   lcd_goto(0, 2);
   strncpy(str, svn_rev_2000 + 6, 6);
   *strchr(str, ' ') = 0;
   printf("  Revision:  %s", str);
   lcd_goto(0, 3);
   strncpy(str, svn_rev_lib + 6, 6);
   *strchr(str, ' ') = 0;
   printf("  Rev. Lib:  %s", str);
}

#pragma NOAREGS

/*---- Front panel button read -------------------------------------*/

bit adc_init = 0;

unsigned char button(unsigned char i)
{
unsigned short xdata value;

   SFRPAGE = ADC0_PAGE;
   if (!adc_init) {
      adc_init = 1;
      SFRPAGE = LEGACY_PAGE;
      REF0CN  = 0x03;           // use internal voltage reference
      AMX0CF  = 0x00;           // select single ended analog inputs
      ADC0CF  = 0x98;           // ADC Clk 2.5 MHz @ 98 MHz, gain 1
      ADC0CN  = 0x80;           // enable ADC 
   }

   AMX0SL  = (7-i) & 0x07;      // set multiplexer
   DELAY_US(2);                 // wait for settling time

   DISABLE_INTERRUPTS;
  
   AD0INT = 0;
   AD0BUSY = 1;
   while (!AD0INT);   // wait until conversion ready

   ENABLE_INTERRUPTS;

   value = (ADC0L | (ADC0H << 8));

   return value < 1000;
}

/*---- EMIF routines -----------------------------------------------*/

void emif_switch(unsigned char bk)
{
   unsigned char d;

   d = (bk & 0x07) << 1; // A16-A18
   if (bk & 0x08)
     d |= 0x20;          // /CS0=low, /CS1=high
   else
     d |= 0x10;          // /CS0=high, /CS1=low

   d |= 0xC0;            // /RD=/WR=high

   P4 = d;
}

void emif_test()
{
   unsigned char idata i, error;
   unsigned char xdata *p;

   lcd_clear();
   lcd_goto(2, 1);
   printf("Memory test...");
   error = 0;

   for (i=0 ; i<16 ; i++) {
      emif_switch(i);

      lcd_goto(1, 0);
      rtc_print();

      lcd_goto(i, 3);
      putchar(218);

      watchdog_refresh(0);

      for (p=0 ; p<0xFFFF ; p++)
         *p = 0;

      for (p=0 ; p<0xFFFF ; p++)
         if (*p != 0) {
            error = 1;
            break;
         }

      lcd_goto(i, 3);
      putchar(217);

      for (p=0 ; p<0xFFFF ; p++)
         *p = 0x55;

      for (p=0 ; p<0xFFFF ; p++)
         if (*p != 0x55) {
            error = 1;
            break;
         }


      lcd_goto(i, 3);
      putchar(216);

      for (p=0 ; p<0xFFFF ; p++)
         *p = 0xAA;

      for (p=0 ; p<0xFFFF ; p++)
         if (*p != 0xAA) {
            error = 1;
            break;
         }

      lcd_goto(i, 3);
      putchar(215);

      for (p=0 ; p<0xFFFF ; p++)
         *p = 0xFF;

      for (p=0 ; p<0xFFFF ; p++)
         if (*p != 0xFF) {
            error = 1;
            break;
         }

      lcd_goto(i, 3);
      putchar(214);
   }

   if (error) {
      lcd_goto(0, 2);
      printf("Memory error bank %bd", i);
      do {
         watchdog_refresh(0);
         b0 = button(0);
      } while (!b0);
   }
}

unsigned char emif_init()
{
unsigned char xdata *p;

   /* setup EMIF interface and probe external memory */
   SFRPAGE = EMI0_PAGE;
   EMI0CF  = 0x3C; // active on P4-P7, non-multiplexed, external only
   EMI0CN  = 0x00; // page zero
   EMI0TC  = 0x04; // 2 SYSCLK cycles (=20ns) /WR and /RD signals

   /* configure EMIF ports as push/pull */
   SFRPAGE = CONFIG_PAGE;
   P4MDOUT = 0xFF;
   P5MDOUT = 0xFF;
   P6MDOUT = 0xFF;
   P7MDOUT = 0xFF;

   /* "park" ports */
   P4 = P5 = P6 = P7 = 0xFF;

   /* test for external memory */
   emif_switch(0);
   p = NULL;
   *p = 0x55;
   if (*p != 0x55) {
      /* turn off EMIF */
      SFRPAGE = EMI0_PAGE;
      EMI0CF  = 0x00;
      return 0;
   }

   /* test for second SRAM chip */
   emif_switch(8);
   *p = 0xAA;
   if (*p == 0xAA) { 
      emif_switch(0);
      return 2;
   }

   emif_switch(0);
   return 1;
}

/*---- Module scan -------------------------------------------------*/

void setup_variables(void)
{
unsigned char xdata port, id, i, j, k, n_var, n_mod, n_var_mod, changed;
char xdata * pvardata;

   /* open drain(*) /push-pull: 
      P0.0 TX1      P1.0 LCD_D1       P2.0 WATCHDOG     P3.0 OPT_CLK
      P0.1*RX1      P1.1 LCD_D2       P2.1 LCD_E        P3.1 OPT_ALE
      P0.2 TX2      P1.2 RTC_IO       P2.2 LCD_RW       P3.2 OPT_STR
      P0.3*RX2      P1.3 RTC_CLK      P2.3 LCD_RS       P3.3 OPT_DATAO
                                                                      
      P0.4 EN1      P1.4              P2.4 LCD_DB4      P3.4*OPT_DATAI
      P0.5 EN2      P1.5              P2.5 LCD_DB5      P3.5*OPT_STAT
      P0.6 LED1     P1.6              P2.6 LCD_DB6      P3.6*OPT_SPARE1
      P0.7 LED2     P1.7 BUZZER       P2.7 LCD_DB7      P3.7*OPT_SPARE2
    */
   SFRPAGE = CONFIG_PAGE;
   P0MDOUT = 0xF5;
   P1MDOUT = 0xFF;
   P2MDOUT = 0xFF;
   P3MDOUT = 0x0F;

   /* enable ADC & DAC */
   SFRPAGE = ADC0_PAGE;
   AMX0CF = 0x00;               // select single ended analog inputs
   ADC0CF = 0x98;               // ADC Clk 2.5 MHz @ 98 MHz, gain 1
   ADC0CN = 0x80;               // enable ADC 
   REF0CN = 0x00;               // use external voltage reference

   SFRPAGE = LEGACY_PAGE;
   REF0CN = 0x03;               // select internal voltage reference

   SFRPAGE = DAC0_PAGE;
   DAC0CN = 0x80;               // enable DAC0
   SFRPAGE = DAC1_PAGE;
   DAC1CN = 0x80;               // enable DAC1

   /* initizlize real time clock */
   rtc_init();

   /* initialize external memory interface */
   memsize = emif_init();

   /* do memory test on cold start */
   SFRPAGE = LEGACY_PAGE;
   if (memsize > 0 /*&& (RSTSRC & 0x02) > 0*/) {
      emif_test();
      sysclock_reset();
   }

   changed = 0;
   n_var = n_mod = 0;
   pvardata = (char *)user_data;

   /* set "variables" pointer to array in xdata */
   variables = vars;

   for (port=0 ; port<N_PORT ; port++) {
      module_id[port] = 0;
      module_nvars[port] = 0;
      module_index[port] = 0xFF;
      read_eeprom(port/8, port%8, &id);

      if (id > 0) {
   
         for (i=0 ; scs_2000_module[i].id ; i++) {
            if (scs_2000_module[i].id == id)
               break;
         }

         if (scs_2000_module[i].id == id) {
            module_nvars[port] = 0;
            for (k=0 ; k<scs_2000_module[i].n_var ; k++) {
               n_var_mod = (unsigned char)scs_2000_module[i].var[k].ud;
               
               if (n_var_mod) {
                  /* clone single variable definition */
                  for (j=0 ; j<n_var_mod ; j++) {
                     memcpy(variables+n_var, &scs_2000_module[i].var[0], sizeof(MSCB_INFO_VAR));
                     if (strchr(variables[n_var].name, '%')) {
                        if (port > 9)
                           *strchr(variables[n_var].name, '%') = 'A'-10+port;
                        else
                           *strchr(variables[n_var].name, '%') = '0'+port;
                     }
                     if (strchr(variables[n_var].name, '#'))
                        *strchr(variables[n_var].name, '#') = '0'+j;
                     variables[n_var].ud = pvardata;
                     pvardata += variables[n_var].width;
                     n_var++;
                     module_nvars[port]++;
                  }
               } else {
                  /* copy over variable definition */
                  memcpy(variables+n_var, &scs_2000_module[i].var[k], sizeof(MSCB_INFO_VAR));
                  if (strchr(variables[n_var].name, '%')) {
                     if (port > 9)
                        *strchr(variables[n_var].name, '%') = 'A'-10+port;
                     else
                        *strchr(variables[n_var].name, '%') = '0'+port;
                  }
                  variables[n_var].ud = pvardata;
                  pvardata += variables[n_var].width;
                  n_var++;
                  module_nvars[port]++;
               }
            }

            module_id[port] = id;
            module_index[port] = i;
            n_mod++;
         
         } else {

            /* initialize new/unknown module */
            lcd_clear();
            lcd_goto(0, 0);
            printf("New module in port %bd", port);
            lcd_goto(0, 1);
            printf("Plese select:");

            i = 0;
            while (1) {
               lcd_goto(0, 2);
               printf(">%02bX %s            ", scs_2000_module[i].id, scs_2000_module[i].name);

               lcd_goto(0, 3);
               if (i == 0)
                  printf("SEL             NEXT");
               else if (scs_2000_module[i+1].id == 0)
                  printf("SEL        PREV     ");
               else
                  printf("SEL        PREV NEXT");

               do {
                  watchdog_refresh(0);
                  b0 = button(0);
                  b1 = button(1);
                  b2 = button(2);
                  b3 = button(3);
               } while (!b0 && !b1 && !b2 && !b3);

               if (b0) {
                  /* write module id and re-evaluate module */
                  write_eeprom(port/8, port%8, scs_2000_module[i].id);
                  port--;
                  while (b0) b0 = button(0);
                  changed = 1;
                  break;
               }

               if (b2) {
                  if (i > 0)
                     i--;
                  while (b2) b2 = button(2);
               }

               if (b3) {
                  if (scs_2000_module[i+1].id)
                     i++;
                  while (b3) b3 = button(3);
               }
            }
         }
      }
   }

   /* mark end of variables list */
   variables[n_var].width = 0;

   /* reboot if any module id has been written */
   if (changed) {
      SFRPAGE = LEGACY_PAGE;
      RSTSRC  = 0x10;         // force software reset
   }

}

/*---- User write function -----------------------------------------*/

void user_write(unsigned char index) reentrant
{
   /* will be updated in main loop */
   update_data[index] = 1;
}

/*---- User read function ------------------------------------------*/

unsigned char user_read(unsigned char index)
{
   if (index);
   return 0;
}

/*---- User function called vid CMD_USER command -------------------*/

unsigned char user_func(unsigned char *data_in, unsigned char *data_out)
{
   /* erase module eeprom of specific port */
   erase_module = data_in[0];
   data_out[0] = 1;
   return 1;
}

/*---- Power management --------------------------------------------*/

bit trip_5V_old = 0, trip_24V_old = 0;

unsigned char power_management()
{
static unsigned long xdata last_pwr;
unsigned char status, i;

   /* only 10 Hz */
   if (time() > last_pwr+10 || time() < last_pwr) {
      last_pwr = time();
    
      for (i=0 ; i<N_PORT/8 ; i++) {

         status = power_mgmt(i, 0);
         
         if ((status & 0x01) == 0) {
            if (!trip_5V_old)
               lcd_clear();
            led_blink(1, 1, 100);
            lcd_goto(0, 0);
            printf("Overcurrent >0.5A on");
            lcd_goto(0, 1);
            printf("    5V output !!!   ");
            trip_5V_old = 1;
            return 1;
         } else if (trip_5V_old) {
            trip_5V_old = 0;
            lcd_clear();
         }
         
         if ((status & 0x02) == 0) {
            if (!trip_24V_old)
               lcd_clear();
            led_blink(1, 1, 100);
            lcd_goto(0, 0);
            printf("   Overcurrent on   ");
            lcd_goto(0, 1);
            printf("   24V output !!!   ");
            lcd_goto(0, 3);
            printf("RESET               ");
         
            if (button(0)) {
               power_mgmt(i, 1);  // issue a reset
               while (button(0)); // wait for button to be released
            }
         
            trip_24V_old = 1;
            return 1;
         } else if (trip_24V_old) {
            trip_24V_old = 0;
            lcd_clear();
         }
      }
   }
  
  return 0;
}

/*---- Application display -----------------------------------------*/

bit b0_old = 0, b1_old = 0, b2_old = 0, b3_old = 0;
unsigned long xdata last_b2 = 0, last_b3 = 0;

unsigned char application_display(bit init)
{
static bit next, prev;
static unsigned char xdata index, last_index, port;
unsigned char xdata i, j, n, col;

   if (init) {
      lcd_clear();
      index = 0;
      last_index = 1;
   }

   if (!master) {
      lcd_goto(5, 1);
      printf("SLAVE MODE");
      return 0;
   }

   if (power_management()) {
      last_index = 255; // force re-display after trip finished
      return 0;
   }

   /* display list of modules */
   if (index != last_index) {

      next = prev = 0;

      for (n=i=0 ; i<N_PORT  ; i++);
         if (module_id[i])
            n++;

      for (col=i=0 ; i<N_PORT ; i++) {

         if (!module_id[i])
            continue;

         if (i < index)
            continue;

         lcd_goto(0, col);
         for (j=0 ; scs_2000_module[j].id && scs_2000_module[j].id != module_id[i]; j++);
         if (scs_2000_module[j].id) {
            if (col == 3) {
               next = 1;
               break;
            }
            printf("P%bd:%02bX %s          ", i, scs_2000_module[j].id, scs_2000_module[j].name);
         } else
            printf("                    ");

         col++;
      }

      lcd_goto(0, 3);
      puts("VARS");

      lcd_goto(16, 3);
      if (next)
         printf("  %c ", 0x13);
      else
         puts("    ");
        
      lcd_goto(10, 3);
      if (index > 0)
         printf("  %c ", 0x12);
      else
         puts("    ");

      last_index = index;
   }


   if (next && b3 && !b3_old)
      index++;

   if (index > 0 && b2 && !b2_old)
      index--;

   /* enter menu on release of button 0 */
   if (!init && !b0 && b0_old) {
      last_index = index+1;
      return 1;
   }

   /* erase EEPROM on release of button 1 (hidden functionality) */
   if (!init && !b1 && b1_old) {

      for (port=0,i=0 ; port<N_PORT ; port++)
         if (module_present(port/8, port%8))
            i++;

      for (port=0 ; port<N_PORT ; port++)
         if (module_present(port/8, port%8))
            break;

      if (i == 0)
         return 0;
      
      lcd_clear();

      lcd_goto(0, 3);
      if (i == 0)
         printf("ESC ERASE           ");
      else
         printf("ESC ERASE  PREV NEXT");

      while (1) {

         lcd_goto(0, 0);
         printf("    Erase module    ");
         lcd_goto(0, 1);
         printf("    in port %bd ?   ", port);

         do {
            watchdog_refresh(0);
            b0 = button(0);
            b1 = button(1);
            b2 = button(2);
            b3 = button(3);
         } while (!b0 && !b1 && !b2 && !b3);

         if (b0) {
            while (b0) b0 = button(0);
            lcd_clear();
            last_index = 255; // force re-display
            b0_old = b1_old = 0;
            return 0;
         }
         
         if (b1) {
            write_eeprom(0, port, 0xFF);
            while (b1) b1 = button(1);
            SFRPAGE = LEGACY_PAGE;
            RSTSRC  = 0x10;   // force software reset
            break;
         }

         if (b2) {
            port = (port+N_PORT-1) % N_PORT;

            while (!module_present(port/8, N_PORT))
               port = (port+N_PORT-1) % N_PORT;

            while (b2) b2 = button(2);
         }

         if (b3) {
            port = (port+1) % N_PORT;

            while (!module_present(port/8, port%8))
               port = (port+1) % N_PORT;

            while (b3) b3 = button(3);
         }
      }
   }

   b0_old = b0;
   b1_old = b1;
   b2_old = b2;
   b3_old = b3;

   return 0;
}

/*---- User loop function ------------------------------------------*/

static unsigned char idata port_index = 0, first_var_index = 0;

void user_loop(void)
{
unsigned char xdata index, i, j, n, port;
float xdata value;

   if (master) {

      /* check if variabled needs to be written */
      for (index=0 ; index<N_PORT*8 ; index++) {
         if (update_data[index]) {
            /* find module to which this variable belongs */
            i = 0;
            for (port=0 ; port<N_PORT ; port++) {
               if (i + module_nvars[port] > index) {
                  i = index - i;
                  j = module_index[port];
         
                  if (j != 0xFF && scs_2000_module[j].driver)
                     scs_2000_module[j].driver(scs_2000_module[j].id, MC_WRITE, 
                                               port/8, port%8, i, variables[index].ud);
         
                  break;
         
               } else
                  i += module_nvars[port];
            }
            update_data[index] = 0;
         }
      }
   
      /* read next port */
      if (module_index[port_index] != 0xFF) {
   
         i = module_index[port_index];
         for (j=0 ; j<module_nvars[port_index] ; j++) {
            if (scs_2000_module[i].driver)
               n = scs_2000_module[i].driver(scs_2000_module[i].id, MC_READ, 
                                             port_index/8, port_index%8, j, &value);
            else
               n = 0;
   
            if (n>0) {
               DISABLE_INTERRUPTS;
               memcpy(variables[first_var_index+j].ud, &value, n);
               ENABLE_INTERRUPTS;
            }
         }
      }
   
      /* go to next port */
      first_var_index += module_nvars[port_index];
      port_index++;
      if (port_index == N_PORT) {
         port_index = 0;
         first_var_index = 0;
      }
   
      /* check for erase module eeprom */
      if (erase_module != 0xFF) {
         write_eeprom(erase_module/8, erase_module%8, 0xFF);
         erase_module = 0xFF;
      }
   
      /* backup data */
      memcpy(&backup_data, &user_data, sizeof(user_data));
   } /* if (master)

   /* read buttons */
   b0 = button(0);
   b1 = button(1);
   b2 = button(2);
   b3 = button(3);

   /* manage menu on LCD display */
   lcd_menu();
}
