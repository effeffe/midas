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
      // create "Demo" image
      midas::odb::create("/History/Images/Demo", TID_KEY);
      h.connect("/History/Images");
   }

   midas::odb::set_debug(false);

   // loop over all cameras
   for (auto &ic: h) {

      // write default values if not present (ODB has precedence)
      midas::odb c = {
         {"Name", "Demo Camera"},
         {"Enabled", false},
         {"URL", "https://localhost:8000/image.jpg"},
         {"Period", 60},
         {"Last fetch", 0},
         {"Storage hours", 72},
         {"Error interval (s)", 60},
         {"Last error", 0}
      };
      c.connect(ic.odb().get_full_path());

      if (!c["Enabled"])
         continue;

      if (ss_time() > c["Last fetch"] + c["Period"]) {
         std::string name = c["Name"];
         std::string url = c["URL"];
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
         curl_easy_setopt(conn, CURLOPT_URL, url.c_str());
         curl_easy_setopt(conn, CURLOPT_WRITEFUNCTION, write_data);
         curl_easy_setopt(conn, CURLOPT_VERBOSE, 0L);
         auto f = fopen(filename.c_str(), "wb");
         if (f) {
            curl_easy_setopt(conn, CURLOPT_WRITEDATA, f);
            curl_easy_setopt(conn, CURLOPT_TIMEOUT, 10L);
            int status = curl_easy_perform(conn);
            fclose(f);
            std::string error;
            if (status == CURLE_COULDNT_CONNECT) {
               error = "Cannot connect to camera \"" + name + "\" at " + url + ", please check camera power and URL";
            } else if (status != CURLE_OK) {
               error = "Error fetching image from camera \"" + name + "\", curl status " + std::to_string(status);
            } else {
               long http_code = 0;
               curl_easy_getinfo(conn, CURLINFO_RESPONSE_CODE, &http_code);
               if (http_code != 200)
                  error = "Error fetching image from camera \"" + name + "\", http error status " +
                          std::to_string(http_code);
            }
            if (!error.empty()) {
               if (ss_time() > c["Last error"] + c["Error interval (s)"]) {
                  cm_msg(MERROR, "log_image_history", "%s", error.c_str());
                  c["Last error"] = ss_time();
               }
            }

         }
         curl_easy_cleanup(conn);
         curl_global_cleanup();

         c["Last fetch"] = ss_time();
      }
   }
}

#else // HAVE_CURL

// no history image logging wihtout CURL library
void log_image_history() {}

#endif