/* This file is part of Clementine.

   Clementine is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   Clementine is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with Clementine.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef GSTENGINEPIPELINE_H
#define GSTENGINEPIPELINE_H

#include <QObject>
#include <QUrl>
#include <QTimeLine>
#include <QBasicTimer>
#include <QMutex>

#include <gst/gst.h>
#include <boost/scoped_ptr.hpp>

#include "engine_fwd.h"

class GstEngine;
class BufferConsumer;

struct GstURIDecodeBin;

class GstEnginePipeline : public QObject {
  Q_OBJECT

 public:
  GstEnginePipeline(GstEngine* engine);
  ~GstEnginePipeline();

  // Call these setters before Init
  void set_output_device(const QString& sink, const QString& device);
  void set_replaygain(bool enabled, int mode, float preamp, bool compression);

  // Creates the pipeline, returns false on error
  bool Init(const QUrl& url);

  // BufferConsumers get fed audio data.  Thread-safe.
  void AddBufferConsumer(BufferConsumer* consumer);
  void RemoveBufferConsumer(BufferConsumer* consumer);
  void RemoveAllBufferConsumers();

  // Control the music playback
  bool SetState(GstState state);
  bool Seek(qint64 nanosec);
  void SetEqualizerEnabled(bool enabled);
  void SetEqualizerParams(int preamp, const QList<int>& band_gains);
  void SetVolume(int percent);
  void StartFader(int duration_msec,
                  QTimeLine::Direction direction = QTimeLine::Forward,
                  QTimeLine::CurveShape shape = QTimeLine::LinearCurve);

  // If this is set then it will be loaded automatically when playback finishes
  // for gapless playback
  void SetNextUrl(const QUrl& url) { next_url_ = url; }

  // Get information about the music playback
  QUrl url() const { return url_; }
  bool is_valid() const { return valid_; }
  qint64 position() const;
  qint64 length() const;
  GstState state() const;

  QUrl redirect_url() const { return redirect_url_; }

 public slots:
  void SetVolumeModifier(qreal mod);

 signals:
  void EndOfStreamReached(bool has_next_track);
  void MetadataFound(const Engine::SimpleMetaBundle& bundle);
  void Error(const QString& message);
  void FaderFinished();

 protected:
  void timerEvent(QTimerEvent *);

 private:
  // Static callbacks.  The GstEnginePipeline instance is passed in the last
  // argument.
  static GstBusSyncReply BusCallbackSync(GstBus*, GstMessage*, gpointer);
  static gboolean BusCallback(GstBus*, GstMessage*, gpointer);
  void TagMessageReceived(GstMessage*);
  void ErrorMessageReceived(GstMessage*);
  void ElementMessageReceived(GstMessage*);
  QString ParseTag(GstTagList* list, const char* tag) const;

  void UpdateVolume();
  bool ReplaceDecodeBin(const QUrl& url);

 private slots:
  void FaderTimelineFinished();

 private:
  static const int kGstStateTimeoutNanosecs;
  static const int kFaderFudgeMsec;

  GstEngine* engine_;

  // General settings for the pipeline
  bool valid_;
  QString sink_;
  QString device_;

  // These get called when there is a new audio buffer available
  QList<BufferConsumer*> buffer_consumers_;
  QMutex buffer_consumers_mutex_;

  // ReplayGain
  bool rg_enabled_;
  int rg_mode_;
  float rg_preamp_;
  bool rg_compression_;

  // The URL that is currently playing, and the URL that is to be preloaded
  // when the current track is close to finishing.
  QUrl url_;
  QUrl next_url_;

  // When the gstreamer source requests a redirect we store the URL here and
  // callers can pick it up after the state change to PLAYING fails.
  QUrl redirect_url_;

  int volume_percent_;
  qreal volume_modifier_;

  boost::scoped_ptr<QTimeLine> fader_;
  QBasicTimer fader_fudge_timer_;

  GstElement* pipeline_;

  // Bins
  // uridecodebin ! audiobin
  GstElement* uridecodebin_;
  GstElement* audiobin_;

  // Elements in the audiobin
  // audioconvert ! rgvolume ! rglimiter ! audioconvert ! equalizer ! volume !
  // audioresample ! audioconvert ! audiosink
  GstElement* audioconvert_;
  GstElement* rgvolume_;
  GstElement* rglimiter_;
  GstElement* audioconvert2_;
  GstElement* equalizer_;
  GstElement* volume_;
  GstElement* audioscale_;
  GstElement* audiosink_;

  uint bus_cb_id_;
};

#endif // GSTENGINEPIPELINE_H
