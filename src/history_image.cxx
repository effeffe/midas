/********************************************************************\

  Name:         history_image.cxx
  Created by:   Stefan Ritt

  Contents:     Logger module saving images from webcams through
                network HTTP link into subdirectories. These images
                can then be retrived in the history page.

\********************************************************************/

#include <string>
#include <exception>
#include <sstream>
#include <iomanip>

#include "midas.h"
#include "msystem.h"
#include "odbxx.hxx"
#include <curl/curl.h>

#ifdef HAVE_CURL

static size_t write_data(void *ptr, size_t size, size_t nmemb, void *stream)
{
   size_t written = fwrite(ptr, size, nmemb, (FILE *)stream);
   return written;
}

void log_image_history() {

   static midas::odb h;
   try {
      if (!h.is_connected())
         h.connect("/History/Images");
      if (h.get_num_values() == 0)
         throw std::runtime_error("");
   } catch(std::exception& e) {
      // create default image tree
      midas::odb hd = {
         {"Demo", {
            {"Name", "Demo Camera"},
            {"URL", "https://localhost:8000/image.jpg"},
            {"Period", 60},
            {"Last fetch", 0},
            {"Storage hours", 72}}
         }
      };
      hd.connect("/History/Images", true);
      h.connect("/History/Images");
   }

   std::string s = h["Demo"]["Name"];
   midas::odb::set_debug(false);

   // loop over all cameras
   for (auto &c: h) {
      midas::odb& ic = c.odb();

      if (ss_time() > ic["Last fetch"] + ic["Period"]) {
         std::string name = ic["Name"];
         std::cout << "Fetching " << name << std::endl;

         midas::odb d("/Logger/Data dir");
         std::string filename = d;
         filename += name;
         mkdir(filename.c_str(), 0755);

         time_t now = time(0);
         tm *ltm = localtime(&now);
         std::stringstream s;
         s <<
            std::setfill('0') << std::setw(2) << ltm->tm_year - 100 <<
            std::setfill('0') << std::setw(2) << ltm->tm_mon + 1 <<
            std::setfill('0') << std::setw(2) << ltm->tm_mday <<
            "_" <<
            std::setfill('0') << std::setw(2) << ltm->tm_hour <<
            std::setfill('0') << std::setw(2) << ltm->tm_min <<
            std::setfill('0') << std::setw(2) << ltm->tm_sec;
         filename += "/" + s.str() + ".jpg";

         curl_global_init(CURL_GLOBAL_DEFAULT);
         CURL *conn = curl_easy_init();
         curl_easy_setopt(conn, CURLOPT_URL, static_cast<const char*>(ic["URL"]));
         curl_easy_setopt(conn, CURLOPT_WRITEFUNCTION, write_data);
         curl_easy_setopt(conn, CURLOPT_VERBOSE, 0L);
         auto f = fopen(filename.c_str(), "wb");
         if (f) {
            curl_easy_setopt(conn, CURLOPT_WRITEDATA, f);
            curl_easy_perform(conn);
            fclose(f);
         }
         curl_easy_cleanup(conn);
         curl_global_cleanup();

         ic["Last fetch"] = ss_time();
      }
   }
}

#else // HAVE_CURL

// no history image logging wihtout CURL library
void log_image_history() {}

#endif