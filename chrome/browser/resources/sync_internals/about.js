// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

cr.define('chrome.sync.about_tab', function() {
  // Contains the latest snapshot of sync about info.
  chrome.sync.aboutInfo = {};

  function refreshAboutInfo(aboutInfo) {
    chrome.sync.aboutInfo = aboutInfo;
    var aboutInfoDiv = $('about-info');
    jstProcess(new JsEvalContext(aboutInfo), aboutInfoDiv);
  }

  function onAboutInfoUpdatedEvent(e) {
    refreshAboutInfo(e.details);
  }

  /** Container for accumulated sync protocol events. */
  var protocolEvents = [];

  /** We may receive re-delivered events.  Keep a record of ones we've seen. */
  var knownEventTimestamps = {};

  /**
   * Callback for incoming protocol events.
   * @param {Event} e The protocol event.
   */
  function onReceivedProtocolEvent(e) {
    var details = e.details;

    // Return early if we've seen this event before.  Assumes that timestamps
    // are sufficiently high resolution to uniquely identify an event.
    if (knownEventTimestamps.hasOwnProperty(details.time)) {
      return;
    }

    knownEventTimestamps[details.time] = true;
    protocolEvents.push(details);

    var context = new JsEvalContext({ events: protocolEvents });
    jstProcess(context, $('traffic-event-container'));
  }

  /**
   * Initializes state and callbacks for the protocol event log UI.
   */
  function initProtocolEventLog() {
    chrome.sync.events.addEventListener(
        'onProtocolEvent', onReceivedProtocolEvent);

    // Make the prototype jscontent element disappear.
    jstProcess({}, $('traffic-event-container'));
  }

  /**
   * Initializes listeners for status dump and import UI.
   */
  function initStatusDumpButton() {
    $('status-data').hidden = true;

    var dumpStatusButton = $('dump-status');
    dumpStatusButton.addEventListener('click', function(event) {
      var aboutInfo = chrome.sync.aboutInfo;
      if (!$('include-ids').checked) {
        aboutInfo.details = chrome.sync.aboutInfo.details.filter(function(el) {
          return !el.is_sensitive;
        });
      }
      var data = '';
      data += new Date().toString() + '\n';
      data += '======\n';
      data += 'Status\n';
      data += '======\n';
      data += JSON.stringify(aboutInfo, null, 2) + '\n';

      $('status-text').value = data;
      $('status-data').hidden = false;
    });

    var importStatusButton = $('import-status');
    importStatusButton.addEventListener('click', function(event) {
      $('status-data').hidden = false;
      if ($('status-text').value.length == 0) {
        $('status-text').value =
            'Paste sync status dump here then click import.';
        return;
      }

      // First remove any characters before the '{'.
      var data = $('status-text').value;
      var firstBrace = data.indexOf('{');
      if (firstBrace < 0) {
        $('status-text').value = 'Invalid sync status dump.';
        return;
      }
      data = data.substr(firstBrace);

      // Remove listeners to prevent sync events from overwriting imported data.
      chrome.sync.events.removeEventListener(
          'onAboutInfoUpdated',
          onAboutInfoUpdatedEvent);

      var aboutInfo = JSON.parse(data);
      refreshAboutInfo(aboutInfo);
    });
  }

  /**
   * Toggles the given traffic event entry div's "expanded" state.
   * @param {HTMLElement} element the element to toggle.
   */
  function expandListener(element) {
    element.target.classList.toggle('traffic-event-entry-expanded');
  }

  /**
   * Attaches a listener to the given traffic event entry div.
   * @param {HTMLElement} element the element to attach the listener to.
   */
  function addExpandListener(element) {
    element.addEventListener('click', expandListener, false);
  }

  function onLoad() {
    initStatusDumpButton();
    initProtocolEventLog();

    chrome.sync.events.addEventListener(
        'onAboutInfoUpdated',
        onAboutInfoUpdatedEvent);

    // Register to receive a stream of event notifications.
    chrome.sync.registerForEvents();

    // Request an about info update event to initialize the page.
    chrome.sync.requestUpdatedAboutInfo();
  }

  return {
    onLoad: onLoad,
    addExpandListener: addExpandListener
  };
});

document.addEventListener(
    'DOMContentLoaded', chrome.sync.about_tab.onLoad, false);
