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
#include <thread>
#include <algorithm>

#include "midas.h"
#include "msystem.h"
#include "odbxx.h"

std::string history_dir() {
   static std::string dir;

   if (dir.empty()) {
      midas::odb o = {
         {"History dir", ""}
      };
      o.connect("/Logger/History/IMAGE");

      if (o["History dir"] != std::string(""))
         dir = o["History dir"];
      else {
         midas::odb l("/Logger");
         if (l.is_subkey("History dir")) {
            dir = l["History dir"];
            if (dir == "")
               dir = l["Data dir"];
         } else
            dir = l["Data dir"];
      }

      if (dir.back() != '/')
         dir += "/";
   }
   return dir;
}

#ifdef HAVE_CURL
#include <curl/curl.h>

static size_t write_data(void *ptr, size_t size, size_t nmemb, void *stream)
{
   size_t written = fwrite(ptr, size, nmemb, (FILE *)stream);
   return written;
}

static std::vector<std::thread> _image_threads;
static bool stop_all_threads = false;

int mkpath(std::string dir, mode_t mode)
{
   if (dir.back() == DIR_SEPARATOR)
      dir.pop_back();

   struct stat sb;
   if (!stat(dir.c_str(), &sb))
      return 0;

   std::string p = dir;
   if (p.find_last_of(DIR_SEPARATOR) != std::string::npos) {
      p = p.substr(0, p.find_last_of(DIR_SEPARATOR));
      mkpath(p.c_str(), mode);
   }

   return mkdir(dir.c_str(), mode);
}

void image_thread(std::string name) {
   DWORD last_check_delete = 0;
   midas::odb o("/History/Images/"+name);

   do {
      std::this_thread::sleep_for(std::chrono::seconds(1));

      // check for old files
      if (ss_time() > last_check_delete + 60 && o["Storage hours"] > 0) {

         std::string path = history_dir();
         path += name;

         char *flist = NULL;
         int n = ss_file_find(path.c_str(), "??????_??????.*", &flist);

         for (int i=0 ; i<n ; i++) {
            char filename[MAX_STRING_LENGTH];
            strncpy(filename, flist+i*MAX_STRING_LENGTH, MAX_STRING_LENGTH);
            struct tm ti{};
            sscanf(filename, "%2d%2d%2d_%2d%2d%2d", &ti.tm_year, &ti.tm_mon,
                   &ti.tm_mday, &ti.tm_hour, &ti.tm_min, &ti.tm_sec);
            ti.tm_year += 100;
            ti.tm_mon -= 1;
            ti.tm_isdst = -1;
            time_t ft = mktime(&ti);
            if ((ss_time() - ft)/3600.0 >= o["Storage hours"]) {
               std::cout << "Delete file " << flist+i*MAX_STRING_LENGTH << " which is is " <<
                  (ss_time()-ft)/3600.0 << " hours old." << std::endl;
               int status = remove((path+"/"+filename).c_str());
               if (status)
                  cm_msg(MERROR, "image_thread", "Cannot remove file %s, status = %d", flist+i*MAX_STRING_LENGTH, status);
            }

         }
         free(flist);

         last_check_delete = ss_time();
      }

      if (!o["Enabled"])
         continue;

      if (ss_time() >= o["Last fetch"] + o["Period"]) {
         o["Last fetch"] = ss_time();
         std::string url = o["URL"];
         std::string filename = history_dir() + name;
         std::string dotname = filename;
         int status = mkpath(filename, 0755);
         if (status)
            cm_msg(MERROR, "image_thread", "Cannot create directory \"%s\": mkpath() errno %d (%s)", filename.c_str(), errno, strerror(errno));

         time_t now = time(nullptr);
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
         filename += "/" + s.str();
         dotname += "/." + s.str();

         if (o["Extension"] == std::string("")) {
            filename += url.substr(url.find_last_of('.'));
            dotname += url.substr(url.find_last_of('.'));
         } else {
            filename += o["Extension"];
            dotname +=  o["Extension"];
         }

         CURL *conn = curl_easy_init();
         curl_easy_setopt(conn, CURLOPT_URL, url.c_str());
         curl_easy_setopt(conn, CURLOPT_WRITEFUNCTION, write_data);
         curl_easy_setopt(conn, CURLOPT_VERBOSE, 0L);
         auto f = fopen(dotname.c_str(), "wb");
         if (f) {
            curl_easy_setopt(conn, CURLOPT_WRITEDATA, f);
            curl_easy_setopt(conn, CURLOPT_TIMEOUT, 60L);
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
               if (ss_time() > o["Last error"] + o["Error interval (s)"]) {
                  cm_msg(MERROR, "log_image_history", "%s", error.c_str());
                  o["Last error"] = ss_time();
               }
               remove(dotname.c_str());
            }

            // rename dotfile to filename to make it visible
            rename(dotname.c_str(), filename.c_str());
         }
         curl_easy_cleanup(conn);
      }

   } while (!stop_all_threads);
}

void stop_image_history() {
   stop_all_threads = true;
   for (auto &t : _image_threads)
      t.join();
   curl_global_cleanup();
}

void start_image_history() {
   curl_global_init(CURL_GLOBAL_DEFAULT);

   // create defautl "Demo" image if ODB treee does not exist
   if (!midas::odb::exists("/History/Images"))
      midas::odb::create("/History/Images/Demo", TID_KEY);

   static midas::odb h;
   h.connect("/History/Images");

   // loop over all cameras
   for (auto &ic: h) {

      // write default values if not present (ODB has precedence)
      midas::odb c = {
              {"Name",               "Demo Camera"},
              {"Enabled",            false},
              {"URL",                "https://localhost:8000/image.jpg"},
              {"Extension",          ".jpg"},
              {"Period",             60},
              {"Last fetch",         0},
              {"Storage hours",      72},
              {"Error interval (s)", 60},
              {"Last error",         0},
              {"Timescale",          "8h"}
      };
      c.connect(ic.get_odb().get_full_path());

      std::string name = ic.get_odb().get_name();
      _image_threads.push_back(std::thread(image_thread, name));
   }
}

#else // HAVE_CURL

// no history image logging wihtout CURL library
void start_image_history() {}
void stop_image_history() {}

#endif

//std::chrono::time_point<std::chrono::high_resolution_clock> usStart()
//{
//   return std::chrono::high_resolution_clock::now();
//}
//
//unsigned int usSince(std::chrono::time_point<std::chrono::high_resolution_clock> start) {
//   auto elapsed = std::chrono::high_resolution_clock::now() - start;
//   return std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count();
//}

// retrieve image history
int hs_image_retrieve(std::string image_name, time_t start_time, time_t stop_time,
                      std::vector<time_t> &vtime, std::vector<std::string> &vfilename)
{
   // auto start = usStart();
   std::string path = history_dir() + image_name;

   char *flist;
   std::vector<std::string> vfn;

   std::string mask;
   if (start_time == stop_time) {
      tm *ltm = localtime(&start_time);
      std::stringstream s;
      s <<
        std::setfill('0') << std::setw(2) << ltm->tm_year - 100 <<
        std::setfill('0') << std::setw(2) << ltm->tm_mon + 1 <<
        std::setfill('0') << std::setw(2) << ltm->tm_mday <<
        "_" << "??????.*";
      mask = s.str();
   } else {

      tm ltStart, ltStop;
      memcpy(&ltStart, localtime(&start_time), sizeof(tm));
      memcpy(&ltStop, localtime(&stop_time), sizeof(tm));
      std::stringstream sStart, sStop;
      std::string mStart, mStop;
      mask = "??????_??????.*";

      sStart <<
        std::setfill('0') << std::setw(2) << ltStart.tm_year - 100 <<
        std::setfill('0') << std::setw(2) << ltStart.tm_mon + 1 <<
        std::setfill('0') << std::setw(2) << ltStart.tm_mday <<
        "_" <<
        std::setfill('0') << std::setw(2) << ltStart.tm_hour <<
        std::setfill('0') << std::setw(2) << ltStart.tm_min <<
        std::setfill('0') << std::setw(2) << ltStart.tm_sec;
      mStart = sStart.str();
      sStop <<
        std::setfill('0') << std::setw(2) << ltStop.tm_year - 100 <<
        std::setfill('0') << std::setw(2) << ltStop.tm_mon + 1 <<
        std::setfill('0') << std::setw(2) << ltStop.tm_mday <<
        "_" <<
        std::setfill('0') << std::setw(2) << ltStop.tm_hour <<
        std::setfill('0') << std::setw(2) << ltStop.tm_min <<
        std::setfill('0') << std::setw(2) << ltStop.tm_sec;
      mStop = sStop.str();
      for (int i=0 ; i<13 ; i++) {
         if (mStart[i] == mStop[i])
            mask[i] = mStart[i];
         else
            break;
      }
   }

   int n = ss_file_find(path.c_str(), mask.c_str(), &flist);

   if (n == 0)
      n = ss_file_find(path.c_str(), "??????_??????.*", &flist);

   for (int i=0 ; i<n ; i++) {
      char filename[MAX_STRING_LENGTH];
      strncpy(filename, flist + i * MAX_STRING_LENGTH, MAX_STRING_LENGTH);
      vfn.push_back(filename);
   }
   std::sort(vfn.begin(), vfn.end());

   time_t minDiff = 1E7;
   time_t minTime{};
   int minIndex{};

   for (int i=0 ; i<n ; i++) {
      struct tm ti{};
      sscanf(vfn[i].c_str(), "%2d%2d%2d_%2d%2d%2d", &ti.tm_year, &ti.tm_mon,
             &ti.tm_mday, &ti.tm_hour, &ti.tm_min, &ti.tm_sec);
      ti.tm_year += 100;
      ti.tm_mon -= 1;
      ti.tm_isdst = -1;
      time_t ft = mktime(&ti);
      time_t now;
      time(&now);

      if (abs(ft - start_time) < minDiff) {
         minDiff = abs(ft - start_time);
         minTime = ft;
         minIndex = i;
      }

      if (start_time != stop_time && ft >= start_time && ft <= stop_time) {
         vtime.push_back(ft);
         vfilename.push_back(vfn[i]);
      }
   }

   // start == stop means return single image closest to them
   if (start_time == stop_time && n > 0) {
      vtime.push_back(minTime);
      vfilename.push_back(vfn[minIndex]);
   }

   free(flist);

   //std::cout << "mask = " << mask << ", n = " << n << ", t = " << usSince(start)/1000.0 << " ms" << std::endl;

   return HS_SUCCESS;
}

