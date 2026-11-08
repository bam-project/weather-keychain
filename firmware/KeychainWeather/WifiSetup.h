//==============================================================================
//  Date:     11/07/2026
//  Program:  Weather Keychain
//  File:     WifiSetup.h
//
//  Purpose:  Declare the Wi-Fi setup controller and its connection state.
//==============================================================================

#pragma once

#include <Arduino.h>
#include <DNSServer.h>
#include <WebServer.h>

class WifiSetup {
 public:
  //------------------------------------------------------------------------------
  // begin
  // Purpose:  Configure the setup routes and try saved credentials.
  // Args:     None.
  // Returns:  None.
  //------------------------------------------------------------------------------
  void begin();
  //------------------------------------------------------------------------------
  // tick
  // Purpose:  Advance the connection state a little each loop; avoid long blocking waits.
  // Args:     None.
  // Returns:  None.
  //------------------------------------------------------------------------------
  void tick();
  //------------------------------------------------------------------------------
  // connected
  // Purpose:  Report whether the device has joined the saved Wi-Fi network.
  // Args:     None.
  // Returns:  true if Wi-Fi is connected.
  //------------------------------------------------------------------------------
  bool connected() const;
  //------------------------------------------------------------------------------
  // portalActive
  // Purpose:  Access the portal active state.
  // Args:     None.
  // Returns:  whether the setup portal is active.
  //------------------------------------------------------------------------------
  bool portalActive() const {
    return portalActive_;
  }
  //------------------------------------------------------------------------------
  // portalSsid
  // Purpose:  Access the portal ssid state.
  // Args:     None.
  // Returns:  setup access point name.
  //------------------------------------------------------------------------------
  String portalSsid() const {
    return portalSsid_;
  }
  //------------------------------------------------------------------------------
  // portalPassword
  // Purpose:  Access the portal password state.
  // Args:     None.
  // Returns:  setup access point password.
  //------------------------------------------------------------------------------
  String portalPassword() const {
    return portalPassword_;
  }
  //------------------------------------------------------------------------------
  // portalUrl
  // Purpose:  Access the portal url state.
  // Args:     None.
  // Returns:  local setup page URL.
  //------------------------------------------------------------------------------
  String portalUrl() const {
    return "http://192.168.4.1/";
  }
  //------------------------------------------------------------------------------
  // status
  // Purpose:  Access the status state.
  // Args:     None.
  // Returns:  current connection status message.
  //------------------------------------------------------------------------------
  String status() const {
    return status_;
  }
  //------------------------------------------------------------------------------
  // startPortal
  // Purpose:  Start the temporary Wi-Fi network and local configuration page.
  // Args:     None.
  // Returns:  None.
  //------------------------------------------------------------------------------
  void startPortal();
  //------------------------------------------------------------------------------
  // suspend
  // Purpose:  Turn off the radio between weather updates to reduce power use.
  // Args:     None.
  // Returns:  None.
  //------------------------------------------------------------------------------
  void suspend();
  //------------------------------------------------------------------------------
  // resume
  // Purpose:  Wake the radio and reconnect using saved credentials.
  // Args:     None.
  // Returns:  None.
  //------------------------------------------------------------------------------
  void resume();
  //------------------------------------------------------------------------------
  // sleeping
  // Purpose:  Access the sleeping state.
  // Args:     None.
  // Returns:  whether the radio is suspended.
  //------------------------------------------------------------------------------
  bool sleeping() const {
    return sleeping_;
  }

  //------------------------------------------------------------------------------
  // city
  // Purpose:  Return the saved city search text.
  // Args:     None.
  // Returns:  City or postal-code query entered during setup.
  //------------------------------------------------------------------------------
  String city() const {
    return city_;
  }

 private:
  String city_;

  WebServer server_{IPAddress(192, 168, 4, 1), 80};
  DNSServer dns_;
  bool started_ = false;
  bool sleeping_ = false;
  bool portalActive_ = false;
  bool portalWanted_ = false;
  bool requestPending_ = false;
  bool disconnectRequested_ = false;
  bool connecting_ = false;
  bool wasConnected_ = false;
  bool saveOnSuccess_ = false;
  uint32_t requestedAt_ = 0;
  uint32_t requestDelayMs_ = 0;
  uint32_t attemptStarted_ = 0;
  uint32_t lastPortalAttempt_ = 0;
  String portalSsid_;
  String portalPassword_;
  String csrfToken_;
  String requestedSsid_;
  String requestedPassword_;
  String status_ = "Wi-Fi not started";

  //------------------------------------------------------------------------------
  // loadCredentials
  // Purpose:  Read saved credentials and reject incomplete or invalid stored data.
  // Args:     None.
  // Returns:  true if valid saved credentials were found.
  //------------------------------------------------------------------------------
  bool loadCredentials();
  //------------------------------------------------------------------------------
  // saveCredentials
  // Purpose:  Save credentials only after a successful connection.
  // Args:     None.
  // Returns:  true if credentials were saved successfully.
  //------------------------------------------------------------------------------
  bool saveCredentials();
  //------------------------------------------------------------------------------
  // queueConnection
  // Purpose:  Schedule a connection attempt without blocking the setup page.
  // Args:     delayMs - delay before attempting the connection
  //           saveOnSuccess - whether to save credentials after connecting
  // Returns:  None.
  //------------------------------------------------------------------------------
  void queueConnection(uint32_t delayMs, bool saveOnSuccess);
  //------------------------------------------------------------------------------
  // stopPortal
  // Purpose:  Stop the setup web server and temporary access point.
  // Args:     None.
  // Returns:  None.
  //------------------------------------------------------------------------------
  void stopPortal();
  //------------------------------------------------------------------------------
  // allowApRequest
  // Purpose:  Accept setup requests only through the device access point.
  // Args:     None.
  // Returns:  true if the request arrived through the setup access point.
  //------------------------------------------------------------------------------
  bool allowApRequest();
  //------------------------------------------------------------------------------
  // canonicalHost
  // Purpose:  Check that the browser is addressing the setup portal directly.
  // Args:     None.
  // Returns:  true if the request uses the portal host.
  //------------------------------------------------------------------------------
  bool canonicalHost() const;
  //------------------------------------------------------------------------------
  // responseHeaders
  // Purpose:  Prevent setup pages and sensitive form data from being cached.
  // Args:     None.
  // Returns:  None.
  //------------------------------------------------------------------------------
  void responseHeaders();
  //------------------------------------------------------------------------------
  // showForm
  // Purpose:  Serve the Wi-Fi form with the current session token.
  // Args:     None.
  // Returns:  None.
  //------------------------------------------------------------------------------
  void showForm();
  //------------------------------------------------------------------------------
  // handleSave
  // Purpose:  Validate the submitted session and credentials before trying the network.
  // Args:     None.
  // Returns:  None.
  //------------------------------------------------------------------------------
  void handleSave();
  //------------------------------------------------------------------------------
  // redirectToPortal
  // Purpose:  Send captive-portal browser requests to the local setup page.
  // Args:     None.
  // Returns:  None.
  //------------------------------------------------------------------------------
  void redirectToPortal();
};
