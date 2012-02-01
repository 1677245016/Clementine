/* This file is part of Clementine.
   Copyright 2010, David Sansome <me@davidsansome.com>

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

#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "core/artloader.h"
#include "core/backgroundstreams.h"
#include "core/commandlineoptions.h"
#include "core/database.h"
#include "core/deletefiles.h"
#include "core/globalshortcuts.h"
#include "core/mac_startup.h"
#include "core/mergedproxymodel.h"
#include "core/mimedata.h"
#include "core/modelfuturewatcher.h"
#include "core/mpris_common.h"
#include "core/network.h"
#include "core/player.h"
#include "core/songloader.h"
#include "core/stylesheetloader.h"
#include "core/taskmanager.h"
#include "core/utilities.h"
#include "devices/devicemanager.h"
#include "devices/devicestatefiltermodel.h"
#include "devices/deviceview.h"
#include "engines/enginebase.h"
#include "engines/gstengine.h"
#include "library/groupbydialog.h"
#include "library/library.h"
#include "library/librarybackend.h"
#include "library/libraryconfig.h"
#include "library/librarydirectorymodel.h"
#include "library/libraryfilterwidget.h"
#include "library/libraryviewcontainer.h"
#include "musicbrainz/fingerprinter.h"
#include "musicbrainz/tagfetcher.h"
#include "playlist/playlistbackend.h"
#include "playlist/playlist.h"
#include "playlist/playlistmanager.h"
#include "playlist/playlistsequence.h"
#include "playlist/playlistview.h"
#include "playlist/queue.h"
#include "playlist/queuemanager.h"
#include "playlist/songplaylistitem.h"
#include "playlistparsers/playlistparser.h"
#include "radio/magnatuneservice.h"
#include "radio/radiomodel.h"
#include "radio/radioview.h"
#include "radio/radioviewcontainer.h"
#include "radio/savedradio.h"
#include "scripting/scriptdialog.h"
#include "scripting/scriptmanager.h"
#include "scripting/uiinterface.h"
#include "smartplaylists/generator.h"
#include "smartplaylists/generatormimedata.h"
#include "songinfo/artistinfoview.h"
#include "songinfo/songinfoview.h"
#include "transcoder/transcodedialog.h"
#include "ui/about.h"
#include "ui/addstreamdialog.h"
#include "ui/edittagdialog.h"
#include "ui/equalizer.h"
#include "ui/iconloader.h"
#include "ui/organisedialog.h"
#include "ui/organiseerrordialog.h"
#include "ui/qtsystemtrayicon.h"
#include "ui/settingsdialog.h"
#include "ui/systemtrayicon.h"
#include "ui/trackselectiondialog.h"
#include "ui/windows7thumbbar.h"
#include "version.h"
#include "widgets/errordialog.h"
#include "widgets/fileview.h"
#include "widgets/multiloadingindicator.h"
#include "widgets/osd.h"
#include "widgets/stylehelper.h"
#include "widgets/trackslider.h"

#ifdef Q_OS_DARWIN
# include "ui/macsystemtrayicon.h"
#endif

#ifdef HAVE_LIBLASTFM
# include "radio/lastfmservice.h"
# include "ui/albumcovermanager.h"
#endif

#ifdef HAVE_WIIMOTEDEV
# include "wiimotedev/shortcuts.h"
#endif

#ifdef ENABLE_VISUALISATIONS
# include "visualisations/visualisationcontainer.h"
#endif

#ifdef HAVE_REMOTE
# include "remote/remote.h"
#endif

#include <QCloseEvent>
#include <QDir>
#include <QFileDialog>
#include <QFileSystemModel>
#include <QLinearGradient>
#include <QMenu>
#include <QMessageBox>
#include <QSettings>
#include <QShortcut>
#include <QSignalMapper>
#include <QSortFilterProxyModel>
#include <QStatusBar>
#include <QtDebug>
#include <QTimer>
#include <QUndoStack>

#ifdef Q_OS_WIN32
# include <qtsparkle/Updater>
#endif


#include <cmath>

using boost::shared_ptr;
using boost::scoped_ptr;

#ifdef Q_OS_DARWIN
// Non exported mac-specific function.
void qt_mac_set_dock_menu(QMenu*);
#endif

const char* MainWindow::kSettingsGroup = "MainWindow";
const char* MainWindow::kMusicFilterSpec =
    QT_TR_NOOP("Music (*.mp3 *.ogg *.flac *.mpc *.m4a *.aac *.wma *.mp4 *.spx *.wav)");
const char* MainWindow::kAllFilesFilterSpec =
    QT_TR_NOOP("All Files (*)");

MainWindow::MainWindow(
    BackgroundThread<Database>* database,
    TaskManager* task_manager,
    PlaylistManager* playlist_manager,
    RadioModel* radio_model,
    Player* player,
    SystemTrayIcon* tray_icon,
    OSD* osd,
    ArtLoader* art_loader,
    QWidget* parent)
  : QMainWindow(parent),
    ui_(new Ui_MainWindow),
    thumbbar_(new Windows7ThumbBar(this)),
    tray_icon_(tray_icon),
    osd_(osd),
    task_manager_(task_manager),
    database_(database),
    radio_model_(radio_model),
    playlist_backend_(NULL),
    playlists_(playlist_manager),
    player_(player),
    library_(NULL),
    global_shortcuts_(new GlobalShortcuts(this)),
    remote_(NULL),
    devices_(NULL),
    library_view_(new LibraryViewContainer(this)),
    file_view_(new FileView(this)),
    radio_view_(new RadioViewContainer(this)),
    device_view_(new DeviceView(this)),
    song_info_view_(new SongInfoView(this)),
    artist_info_view_(new ArtistInfoView(this)),
    settings_dialog_(NULL),
#ifdef HAVE_LIBLASTFM
    cover_manager_(NULL),
#endif
    equalizer_(new Equalizer),
    error_dialog_(NULL),
    organise_dialog_(new OrganiseDialog(task_manager_)),
    queue_manager_(NULL),
#ifdef ENABLE_VISUALISATIONS
    visualisation_(NULL),
#endif
#ifdef HAVE_WIIMOTEDEV
    wiimotedev_shortcuts_(NULL),
#endif
    scripts_(new ScriptManager(this)),
    playlist_menu_(new QMenu(this)),
    playlist_add_to_another_(NULL),
    library_sort_model_(new QSortFilterProxyModel(this)),
    track_position_timer_(new QTimer(this)),
    was_maximized_(false),
    doubleclick_addmode_(AddBehaviour_Append),
    doubleclick_playmode_(PlayBehaviour_IfStopped),
    menu_playmode_(PlayBehaviour_IfStopped)
{
  // Database connections
  connect(database_->Worker().get(), SIGNAL(Error(QString)), SLOT(ShowErrorDialog(QString)));

  // Create some objects in the database thread
  playlist_backend_ = new PlaylistBackend;
  playlist_backend_->moveToThread(database_);
  playlist_backend_->SetDatabase(database_->Worker());

  // Create stuff that needs the database
  library_ = new Library(database_, task_manager_, this);
  devices_ = new DeviceManager(database_, task_manager_, this);

  playlist_backend_->SetLibrary(library_->backend());

  // Initialise the UI
  ui_->setupUi(this);
  ui_->multi_loading_indicator->SetTaskManager(task_manager_);
  ui_->now_playing->SetLibraryBackend(library_->backend());

  int volume = player_->GetVolume();
  ui_->volume->setValue(volume);
  VolumeChanged(volume);

  // Add tabs to the fancy tab widget
  ui_->tabs->AddTab(library_view_, IconLoader::Load("folder-sound"), tr("Library"));
  ui_->tabs->AddTab(file_view_, IconLoader::Load("document-open"), tr("Files"));
  ui_->tabs->AddTab(radio_view_, IconLoader::Load("applications-internet"), tr("Internet"));
  ui_->tabs->AddTab(device_view_, IconLoader::Load("multimedia-player-ipod-mini-blue"), tr("Devices"));
  ui_->tabs->AddSpacer();
  ui_->tabs->AddTab(song_info_view_, IconLoader::Load("view-media-lyrics"), tr("Song info"));
  ui_->tabs->AddTab(artist_info_view_, IconLoader::Load("x-clementine-artist"), tr("Artist info"));

  // Add the now playing widget to the fancy tab widget
  ui_->tabs->AddBottomWidget(ui_->now_playing);

  ui_->tabs->SetBackgroundPixmap(QPixmap(":/sidebar_background.png"));
  StyleHelper::setBaseColor(palette().color(QPalette::Highlight).darker());

  track_position_timer_->setInterval(1000);
  connect(track_position_timer_, SIGNAL(timeout()), SLOT(UpdateTrackPosition()));

  // Start initialising the player
  player_->Init();
  background_streams_ = new BackgroundStreams(player_->engine(), this);
  background_streams_->LoadStreams();

  // Models
  library_sort_model_->setSourceModel(library_->model());
  library_sort_model_->setSortRole(LibraryModel::Role_SortText);
  library_sort_model_->setDynamicSortFilter(true);
  library_sort_model_->sort(0);

  connect(ui_->playlist, SIGNAL(ViewSelectionModelChanged()), SLOT(PlaylistViewSelectionModelChanged()));
  ui_->playlist->SetManager(playlists_);

  library_view_->view()->setModel(library_sort_model_);
  library_view_->view()->SetLibrary(library_->model());
  library_view_->view()->SetTaskManager(task_manager_);
  library_view_->view()->SetDeviceManager(devices_);

  radio_view_->SetModel(radio_model_);

  device_view_->SetDeviceManager(devices_);
  device_view_->SetLibrary(library_->model());

  organise_dialog_->SetDestinationModel(library_->model()->directory_model());

  // Icons
  ui_->action_about->setIcon(IconLoader::Load("help-about"));
  ui_->action_about_qt->setIcon(QIcon(":/trolltech/qmessagebox/images/qtlogo-64.png"));
  ui_->action_add_file->setIcon(IconLoader::Load("document-open"));
  ui_->action_add_folder->setIcon(IconLoader::Load("document-open-folder"));
  ui_->action_add_stream->setIcon(IconLoader::Load("document-open-remote"));
  ui_->action_clear_playlist->setIcon(IconLoader::Load("edit-clear-list"));
  ui_->action_configure->setIcon(IconLoader::Load("configure"));
  ui_->action_cover_manager->setIcon(IconLoader::Load("download"));
  ui_->action_edit_track->setIcon(IconLoader::Load("edit-rename"));
  ui_->action_equalizer->setIcon(IconLoader::Load("view-media-equalizer"));
  ui_->action_jump->setIcon(IconLoader::Load("go-jump"));
  ui_->action_next_track->setIcon(IconLoader::Load("media-skip-forward"));
  ui_->action_open_media->setIcon(IconLoader::Load("document-open"));
  ui_->action_play_pause->setIcon(IconLoader::Load("media-playback-start"));
  ui_->action_previous_track->setIcon(IconLoader::Load("media-skip-backward"));
  ui_->action_quit->setIcon(IconLoader::Load("application-exit"));
  ui_->action_remove_from_playlist->setIcon(IconLoader::Load("list-remove"));
  ui_->action_repeat_mode->setIcon(IconLoader::Load("media-playlist-repeat"));
  ui_->action_shuffle->setIcon(IconLoader::Load("x-clementine-shuffle"));
  ui_->action_shuffle_mode->setIcon(IconLoader::Load("media-playlist-shuffle"));
  ui_->action_stop->setIcon(IconLoader::Load("media-playback-stop"));
  ui_->action_stop_after_this_track->setIcon(IconLoader::Load("media-playback-stop"));
  ui_->action_new_playlist->setIcon(IconLoader::Load("document-new"));
  ui_->action_load_playlist->setIcon(IconLoader::Load("document-open"));
  ui_->action_save_playlist->setIcon(IconLoader::Load("document-save"));
  ui_->action_full_library_scan->setIcon(IconLoader::Load("view-refresh"));
  ui_->action_rain->setIcon(IconLoader::Load("weather-showers-scattered"));


  // File view connections
  connect(file_view_, SIGNAL(AddToPlaylist(QMimeData*)), SLOT(AddToPlaylist(QMimeData*)));
  connect(file_view_, SIGNAL(PathChanged(QString)), SLOT(FilePathChanged(QString)));
  connect(file_view_, SIGNAL(CopyToLibrary(QList<QUrl>)), SLOT(CopyFilesToLibrary(QList<QUrl>)));
  connect(file_view_, SIGNAL(MoveToLibrary(QList<QUrl>)), SLOT(MoveFilesToLibrary(QList<QUrl>)));
  connect(file_view_, SIGNAL(CopyToDevice(QList<QUrl>)), SLOT(CopyFilesToDevice(QList<QUrl>)));
  file_view_->SetTaskManager(task_manager_);

  // Action connections
  connect(ui_->action_next_track, SIGNAL(triggered()), player_, SLOT(Next()));
  connect(ui_->action_previous_track, SIGNAL(triggered()), player_, SLOT(Previous()));
  connect(ui_->action_play_pause, SIGNAL(triggered()), player_, SLOT(PlayPause()));
  connect(ui_->action_stop, SIGNAL(triggered()), player_, SLOT(Stop()));
  connect(ui_->action_quit, SIGNAL(triggered()), SLOT(Exit()));
  connect(ui_->action_stop_after_this_track, SIGNAL(triggered()), SLOT(StopAfterCurrent()));
  connect(ui_->action_mute, SIGNAL(triggered()), player_, SLOT(Mute()));
#ifdef HAVE_LIBLASTFM
  connect(ui_->action_ban, SIGNAL(triggered()), RadioModel::Service<LastFMService>(), SLOT(Ban()));
  connect(ui_->action_love, SIGNAL(triggered()), SLOT(Love()));
#endif
  connect(ui_->action_clear_playlist, SIGNAL(triggered()), playlists_, SLOT(ClearCurrent()));
  connect(ui_->action_remove_from_playlist, SIGNAL(triggered()), SLOT(PlaylistRemoveCurrent()));
  connect(ui_->action_edit_track, SIGNAL(triggered()), SLOT(EditTracks()));
  connect(ui_->action_renumber_tracks, SIGNAL(triggered()), SLOT(RenumberTracks()));
  connect(ui_->action_selection_set_value, SIGNAL(triggered()), SLOT(SelectionSetValue()));
  connect(ui_->action_edit_value, SIGNAL(triggered()), SLOT(EditValue()));
  connect(ui_->action_auto_complete_tags, SIGNAL(triggered()), SLOT(AutoCompleteTags()));
  connect(ui_->action_configure, SIGNAL(triggered()), SLOT(OpenSettingsDialog()));
  connect(ui_->action_about, SIGNAL(triggered()), SLOT(ShowAboutDialog()));
  connect(ui_->action_about_qt, SIGNAL(triggered()), qApp, SLOT(aboutQt()));
  connect(ui_->action_shuffle, SIGNAL(triggered()), playlists_, SLOT(ShuffleCurrent()));
  connect(ui_->action_open_media, SIGNAL(triggered()), SLOT(AddFile()));
  connect(ui_->action_add_file, SIGNAL(triggered()), SLOT(AddFile()));
  connect(ui_->action_add_folder, SIGNAL(triggered()), SLOT(AddFolder()));
  connect(ui_->action_add_stream, SIGNAL(triggered()), SLOT(AddStream()));
#ifdef HAVE_LIBLASTFM
  connect(ui_->action_cover_manager, SIGNAL(triggered()), SLOT(ShowCoverManager()));
#else
  ui_->action_cover_manager->setEnabled(false);
#endif
  connect(ui_->action_equalizer, SIGNAL(triggered()), equalizer_.get(), SLOT(show()));
  connect(ui_->action_transcode, SIGNAL(triggered()), SLOT(ShowTranscodeDialog()));
  connect(ui_->action_jump, SIGNAL(triggered()), ui_->playlist->view(), SLOT(JumpToCurrentlyPlayingTrack()));
  connect(ui_->action_update_library, SIGNAL(triggered()), library_, SLOT(IncrementalScan()));
  connect(ui_->action_full_library_scan, SIGNAL(triggered()), library_, SLOT(FullScan()));
  connect(ui_->action_rain, SIGNAL(toggled(bool)),
          background_streams_, SLOT(MakeItRain(bool)));
  connect(ui_->action_hypnotoad, SIGNAL(toggled(bool)),
          background_streams_, SLOT(AllGloryToTheHypnotoad(bool)));
  connect(ui_->action_queue_manager, SIGNAL(triggered()), SLOT(ShowQueueManager()));

  // Give actions to buttons
  ui_->forward_button->setDefaultAction(ui_->action_next_track);
  ui_->back_button->setDefaultAction(ui_->action_previous_track);
  ui_->pause_play_button->setDefaultAction(ui_->action_play_pause);
  ui_->stop_button->setDefaultAction(ui_->action_stop);
  ui_->love_button->setDefaultAction(ui_->action_love);
  ui_->ban_button->setDefaultAction(ui_->action_ban);
  ui_->clear_playlist_button->setDefaultAction(ui_->action_clear_playlist);
  ui_->playlist->SetActions(ui_->action_new_playlist, ui_->action_save_playlist,
                            ui_->action_load_playlist);

#ifdef ENABLE_VISUALISATIONS
  connect(ui_->action_visualisations, SIGNAL(triggered()), SLOT(ShowVisualisations()));
#else
  ui_->action_visualisations->setEnabled(false);
#endif

  // Add the shuffle and repeat action groups to the menu
  ui_->action_shuffle_mode->setMenu(ui_->playlist_sequence->shuffle_menu());
  ui_->action_repeat_mode->setMenu(ui_->playlist_sequence->repeat_menu());

  // Stop actions
  QMenu* stop_menu = new QMenu(this);
  stop_menu->addAction(ui_->action_stop);
  stop_menu->addAction(ui_->action_stop_after_this_track);
  ui_->stop_button->setMenu(stop_menu);

  // Player connections
  connect(ui_->volume, SIGNAL(valueChanged(int)), player_, SLOT(SetVolume(int)));

  connect(player_, SIGNAL(Error(QString)), SLOT(ShowErrorDialog(QString)));
  connect(player_, SIGNAL(SongChangeRequestProcessed(QUrl,bool)), playlists_, SLOT(SongChangeRequestProcessed(QUrl,bool)));

  connect(player_, SIGNAL(Paused()), SLOT(MediaPaused()));
  connect(player_, SIGNAL(Playing()), SLOT(MediaPlaying()));
  connect(player_, SIGNAL(Stopped()), SLOT(MediaStopped()));
  connect(player_, SIGNAL(TrackSkipped(PlaylistItemPtr)), SLOT(TrackSkipped(PlaylistItemPtr)));
  connect(player_, SIGNAL(VolumeChanged(int)), SLOT(VolumeChanged(int)));

  connect(player_, SIGNAL(Paused()), playlists_, SLOT(SetActivePaused()));
  connect(player_, SIGNAL(Playing()), playlists_, SLOT(SetActivePlaying()));
  connect(player_, SIGNAL(Stopped()), playlists_, SLOT(SetActiveStopped()));

  connect(player_, SIGNAL(Paused()), ui_->playlist->view(), SLOT(StopGlowing()));
  connect(player_, SIGNAL(Playing()), ui_->playlist->view(), SLOT(StartGlowing()));
  connect(player_, SIGNAL(Stopped()), ui_->playlist->view(), SLOT(StopGlowing()));
  connect(player_, SIGNAL(Paused()), ui_->playlist, SLOT(ActivePaused()));
  connect(player_, SIGNAL(Playing()), ui_->playlist, SLOT(ActivePlaying()));
  connect(player_, SIGNAL(Stopped()), ui_->playlist, SLOT(ActiveStopped()));

  connect(player_, SIGNAL(Paused()), osd_, SLOT(Paused()));
  connect(player_, SIGNAL(Stopped()), osd_, SLOT(Stopped()));
  connect(player_, SIGNAL(PlaylistFinished()), osd_, SLOT(PlaylistFinished()));
  connect(player_, SIGNAL(VolumeChanged(int)), osd_, SLOT(VolumeChanged(int)));
  connect(player_, SIGNAL(VolumeChanged(int)), ui_->volume, SLOT(setValue(int)));
  connect(player_, SIGNAL(ForceShowOSD(Song)), SLOT(ForceShowOSD(Song)));
  connect(playlists_, SIGNAL(CurrentSongChanged(Song)), SLOT(SongChanged(Song)));
  connect(playlists_, SIGNAL(CurrentSongChanged(Song)), osd_, SLOT(SongChanged(Song)));
  connect(playlists_, SIGNAL(CurrentSongChanged(Song)), player_, SLOT(CurrentMetadataChanged(Song)));
  connect(playlists_, SIGNAL(EditingFinished(QModelIndex)), SLOT(PlaylistEditFinished(QModelIndex)));
  connect(playlists_, SIGNAL(Error(QString)), SLOT(ShowErrorDialog(QString)));
  connect(playlists_, SIGNAL(SummaryTextChanged(QString)), ui_->playlist_summary, SLOT(setText(QString)));
  connect(playlists_, SIGNAL(PlayRequested(QModelIndex)), SLOT(PlayIndex(QModelIndex)));

  connect(ui_->playlist->view(), SIGNAL(doubleClicked(QModelIndex)), SLOT(PlayIndex(QModelIndex)));
  connect(ui_->playlist->view(), SIGNAL(PlayItem(QModelIndex)), SLOT(PlayIndex(QModelIndex)));
  connect(ui_->playlist->view(), SIGNAL(PlayPause()), player_, SLOT(PlayPause()));
  connect(ui_->playlist->view(), SIGNAL(RightClicked(QPoint,QModelIndex)), SLOT(PlaylistRightClick(QPoint,QModelIndex)));
  connect(ui_->playlist->view(), SIGNAL(SeekTrack(int)), ui_->track_slider, SLOT(Seek(int)));

  connect(ui_->track_slider, SIGNAL(ValueChanged(int)), player_, SLOT(SeekTo(int)));

  // Library connections
  connect(library_view_->view(), SIGNAL(AddToPlaylistSignal(QMimeData*)), SLOT(AddToPlaylist(QMimeData*)));
  connect(library_view_->view(), SIGNAL(ShowConfigDialog()), SLOT(ShowLibraryConfig()));
  connect(library_->model(), SIGNAL(TotalSongCountUpdated(int)), library_view_->view(), SLOT(TotalSongCountUpdated(int)));

  connect(task_manager_, SIGNAL(PauseLibraryWatchers()), library_, SLOT(PauseWatcher()));
  connect(task_manager_, SIGNAL(ResumeLibraryWatchers()), library_, SLOT(ResumeWatcher()));

  // Devices connections
  connect(devices_, SIGNAL(Error(QString)), SLOT(ShowErrorDialog(QString)));
  connect(device_view_, SIGNAL(AddToPlaylistSignal(QMimeData*)), SLOT(AddToPlaylist(QMimeData*)));

  // Library filter widget
  QActionGroup* library_view_group = new QActionGroup(this);

  library_show_all_ = library_view_group->addAction(tr("Show all songs"));
  library_show_duplicates_ = library_view_group->addAction(tr("Show only duplicates"));
  library_show_untagged_ = library_view_group->addAction(tr("Show only untagged"));

  library_show_all_->setCheckable(true);
  library_show_duplicates_->setCheckable(true);
  library_show_untagged_->setCheckable(true);
  library_show_all_->setChecked(true);

  connect(library_view_group, SIGNAL(triggered(QAction*)), SLOT(ChangeLibraryQueryMode(QAction*)));

  QAction* library_config_action = new QAction(
      IconLoader::Load("configure"), tr("Configure library..."), this);
  connect(library_config_action, SIGNAL(triggered()), SLOT(ShowLibraryConfig()));
  library_view_->filter()->SetSettingsGroup(kSettingsGroup);
  library_view_->filter()->SetLibraryModel(library_->model());

  QAction* separator = new QAction(this);
  separator->setSeparator(true);

  library_view_->filter()->AddMenuAction(library_show_all_);
  library_view_->filter()->AddMenuAction(library_show_duplicates_);
  library_view_->filter()->AddMenuAction(library_show_untagged_);
  library_view_->filter()->AddMenuAction(separator);
  library_view_->filter()->AddMenuAction(library_config_action);

  // Playlist menu
  playlist_play_pause_ = playlist_menu_->addAction(tr("Play"), this, SLOT(PlaylistPlay()));
  playlist_menu_->addAction(ui_->action_stop);
  playlist_stop_after_ = playlist_menu_->addAction(IconLoader::Load("media-playback-stop"), tr("Stop after this track"), this, SLOT(PlaylistStopAfter()));
  playlist_queue_ = playlist_menu_->addAction("", this, SLOT(PlaylistQueue()));
  playlist_queue_->setShortcut(QKeySequence("Ctrl+D"));
  ui_->playlist->addAction(playlist_queue_);
  playlist_menu_->addSeparator();
  playlist_menu_->addAction(ui_->action_remove_from_playlist);
  playlist_undoredo_ = playlist_menu_->addSeparator();
  playlist_menu_->addAction(ui_->action_edit_track);
  playlist_menu_->addAction(ui_->action_edit_value);
  playlist_menu_->addAction(ui_->action_renumber_tracks);
  playlist_menu_->addAction(ui_->action_selection_set_value);
  playlist_menu_->addAction(ui_->action_auto_complete_tags);
  playlist_menu_->addSeparator();
  playlist_copy_to_library_ = playlist_menu_->addAction(IconLoader::Load("edit-copy"), tr("Copy to library..."), this, SLOT(PlaylistCopyToLibrary()));
  playlist_move_to_library_ = playlist_menu_->addAction(IconLoader::Load("go-jump"), tr("Move to library..."), this, SLOT(PlaylistMoveToLibrary()));
  playlist_organise_ = playlist_menu_->addAction(IconLoader::Load("edit-copy"), tr("Organise files..."), this, SLOT(PlaylistMoveToLibrary()));
  playlist_copy_to_device_ = playlist_menu_->addAction(IconLoader::Load("multimedia-player-ipod-mini-blue"), tr("Copy to device..."), this, SLOT(PlaylistCopyToDevice()));
  playlist_delete_ = playlist_menu_->addAction(IconLoader::Load("edit-delete"), tr("Delete from disk..."), this, SLOT(PlaylistDelete()));
  playlist_open_in_browser_ = playlist_menu_->addAction(IconLoader::Load("document-open-folder"), tr("Show in file browser..."), this, SLOT(PlaylistOpenInBrowser()));
  playlist_menu_->addSeparator();
  playlist_menu_->addAction(ui_->action_clear_playlist);
  playlist_menu_->addAction(ui_->action_shuffle);

#ifdef Q_OS_DARWIN
  ui_->action_shuffle->setShortcut(QKeySequence());
#endif

  // We have to add the actions on the playlist menu to this QWidget otherwise
  // their shortcut keys don't work
  addActions(playlist_menu_->actions());

  connect(ui_->playlist, SIGNAL(UndoRedoActionsChanged(QAction*,QAction*)),
          SLOT(PlaylistUndoRedoChanged(QAction*,QAction*)));

  playlist_copy_to_device_->setDisabled(devices_->connected_devices_model()->rowCount() == 0);
  connect(devices_->connected_devices_model(), SIGNAL(IsEmptyChanged(bool)),
          playlist_copy_to_device_, SLOT(setDisabled(bool)));

  // Radio connections
  connect(radio_model_, SIGNAL(StreamError(QString)), SLOT(ShowErrorDialog(QString)));
  connect(radio_model_, SIGNAL(AsyncLoadFinished(PlaylistItem::SpecialLoadResult)), player_, SLOT(HandleSpecialLoad(PlaylistItem::SpecialLoadResult)));
  connect(radio_model_, SIGNAL(StreamMetadataFound(QUrl,Song)), playlists_, SLOT(SetActiveStreamMetadata(QUrl,Song)));
  connect(radio_model_, SIGNAL(OpenSettingsAtPage(SettingsDialog::Page)), SLOT(OpenSettingsDialogAtPage(SettingsDialog::Page)));
  connect(radio_model_, SIGNAL(AddToPlaylist(QMimeData*)), SLOT(AddToPlaylist(QMimeData*)));
#ifdef HAVE_LIBLASTFM
  connect(RadioModel::Service<LastFMService>(), SIGNAL(ScrobblingEnabledChanged(bool)), SLOT(ScrobblingEnabledChanged(bool)));
  connect(RadioModel::Service<LastFMService>(), SIGNAL(ButtonVisibilityChanged(bool)), SLOT(LastFMButtonVisibilityChanged(bool)));
#endif
  connect(radio_model_->Service<MagnatuneService>(), SIGNAL(DownloadFinished(QStringList)), osd_, SLOT(MagnatuneDownloadFinished(QStringList)));
  connect(radio_view_->tree(), SIGNAL(AddToPlaylistSignal(QMimeData*)), SLOT(AddToPlaylist(QMimeData*)));

#ifdef HAVE_LIBLASTFM
  LastFMButtonVisibilityChanged(RadioModel::Service<LastFMService>()->AreButtonsVisible());
#else
  LastFMButtonVisibilityChanged(false);
#endif

  // Connections to the saved streams service
  connect(RadioModel::Service<SavedRadio>(), SIGNAL(ShowAddStreamDialog()), SLOT(AddStream()));

#ifdef Q_OS_DARWIN
  mac::SetApplicationHandler(this);
#endif
  // Tray icon
  tray_icon_->SetupMenu(ui_->action_previous_track,
                        ui_->action_play_pause,
                        ui_->action_stop,
                        ui_->action_stop_after_this_track,
                        ui_->action_next_track,
                        ui_->action_mute,
                        ui_->action_love,
                        ui_->action_ban,
                        ui_->action_quit);
  connect(tray_icon_, SIGNAL(PlayPause()), player_, SLOT(PlayPause()));
  connect(tray_icon_, SIGNAL(ShowHide()), SLOT(ToggleShowHide()));
  connect(tray_icon_, SIGNAL(ChangeVolume(int)), SLOT(VolumeWheelEvent(int)));

  // Windows 7 thumbbar buttons
  thumbbar_->SetActions(QList<QAction*>()
      << ui_->action_previous_track
      << ui_->action_play_pause
      << ui_->action_stop
      << ui_->action_next_track
      << NULL // spacer
      << ui_->action_love
      << ui_->action_ban);

#if (defined(Q_OS_DARWIN) && defined(HAVE_SPARKLE)) || defined(Q_OS_WIN32)
  // Add check for updates item to application menu.
  QAction* check_updates = ui_->menu_tools->addAction(tr("Check for updates..."));
  check_updates->setMenuRole(QAction::ApplicationSpecificRole);
  connect(check_updates, SIGNAL(triggered(bool)), SLOT(CheckForUpdates()));
#endif

#ifdef Q_OS_DARWIN
  // Force this menu to be the app "Preferences".
  ui_->action_configure->setMenuRole(QAction::PreferencesRole);
  // Force this menu to be the app "About".
  ui_->action_about->setMenuRole(QAction::AboutRole);
  // Force this menu to be the app "Quit".
  ui_->action_quit->setMenuRole(QAction::QuitRole);
#endif

#ifdef Q_OS_WIN32
  qtsparkle::Updater* updater = new qtsparkle::Updater(
      QUrl("http://data.clementine-player.org/sparkle-windows"), this);
  updater->SetNetworkAccessManager(new NetworkAccessManager(this));
  updater->SetVersion(CLEMENTINE_VERSION);
  connect(check_updates, SIGNAL(triggered()), updater, SLOT(CheckNow()));
#endif

  // Global shortcuts
  connect(global_shortcuts_, SIGNAL(Play()), player_, SLOT(Play()));
  connect(global_shortcuts_, SIGNAL(Pause()), player_, SLOT(Pause()));
  connect(global_shortcuts_, SIGNAL(PlayPause()), ui_->action_play_pause, SLOT(trigger()));
  connect(global_shortcuts_, SIGNAL(Stop()), ui_->action_stop, SLOT(trigger()));
  connect(global_shortcuts_, SIGNAL(StopAfter()), ui_->action_stop_after_this_track, SLOT(trigger()));
  connect(global_shortcuts_, SIGNAL(Next()), ui_->action_next_track, SLOT(trigger()));
  connect(global_shortcuts_, SIGNAL(Previous()), ui_->action_previous_track, SLOT(trigger()));
  connect(global_shortcuts_, SIGNAL(IncVolume()), player_, SLOT(VolumeUp()));
  connect(global_shortcuts_, SIGNAL(DecVolume()), player_, SLOT(VolumeDown()));
  connect(global_shortcuts_, SIGNAL(Mute()), player_, SLOT(Mute()));
  connect(global_shortcuts_, SIGNAL(SeekForward()), player_, SLOT(SeekForward()));
  connect(global_shortcuts_, SIGNAL(SeekBackward()), player_, SLOT(SeekBackward()));
  connect(global_shortcuts_, SIGNAL(ShowHide()), SLOT(ToggleShowHide()));
  connect(global_shortcuts_, SIGNAL(ShowOSD()), player_, SLOT(ShowOSD()));

  connect(global_shortcuts_, SIGNAL(RateCurrentSong(int)), playlists_, SLOT(RateCurrentSong(int)));

  // XMPP Remote control
#ifdef HAVE_REMOTE
  remote_ = new Remote(player_, this);
  connect(remote_, SIGNAL(Error(QString)), SLOT(ShowErrorDialog(QString)));
  connect(art_loader, SIGNAL(ArtLoaded(Song,QString,QImage)),
          remote_,      SLOT(ArtLoaded(Song,QString,QImage)));
#endif

  // Fancy tabs
  connect(ui_->tabs, SIGNAL(ModeChanged(FancyTabWidget::Mode)), SLOT(SaveGeometry()));
  connect(ui_->tabs, SIGNAL(CurrentChanged(int)), SLOT(SaveGeometry()));

  // Lyrics
  ConnectInfoView(song_info_view_);
  ConnectInfoView(artist_info_view_);

  // Analyzer
  ui_->analyzer->SetEngine(player_->engine());
  ui_->analyzer->SetActions(ui_->action_visualisations);

  // Equalizer
  connect(equalizer_.get(), SIGNAL(ParametersChanged(int,QList<int>)),
          player_->engine(), SLOT(SetEqualizerParameters(int,QList<int>)));
  connect(equalizer_.get(), SIGNAL(EnabledChanged(bool)),
          player_->engine(), SLOT(SetEqualizerEnabled(bool)));
  player_->engine()->SetEqualizerEnabled(equalizer_->is_enabled());
  player_->engine()->SetEqualizerParameters(
      equalizer_->preamp_value(), equalizer_->gain_values());

  // Statusbar widgets
  ui_->playlist_summary->setMinimumWidth(QFontMetrics(font()).width("WW selected of WW tracks - [ WW:WW ]"));
  ui_->status_bar_stack->setCurrentWidget(ui_->playlist_summary_page);
  connect(ui_->multi_loading_indicator, SIGNAL(TaskCountChange(int)), SLOT(TaskCountChanged(int)));

  // Now playing widget
  ui_->now_playing->set_ideal_height(ui_->status_bar->sizeHint().height() +
                                     ui_->player_controls->sizeHint().height());
  connect(playlists_, SIGNAL(CurrentSongChanged(Song)), ui_->now_playing, SLOT(NowPlaying(Song)));
  connect(player_, SIGNAL(Stopped()), ui_->now_playing, SLOT(Stopped()));
  connect(ui_->now_playing, SIGNAL(ShowAboveStatusBarChanged(bool)),
          SLOT(NowPlayingWidgetPositionChanged(bool)));
  connect(ui_->action_hypnotoad, SIGNAL(toggled(bool)), ui_->now_playing, SLOT(AllHail(bool)));
  connect(ui_->action_kittens, SIGNAL(toggled(bool)), ui_->now_playing, SLOT(EnableKittens(bool)));
  NowPlayingWidgetPositionChanged(ui_->now_playing->show_above_status_bar());

  // Add places where scripts can make actions
  scripts_->ui()->RegisterActionLocation("tools_menu", ui_->menu_tools, ui_->action_full_library_scan);
  scripts_->ui()->RegisterActionLocation("extras_menu", ui_->menu_extras, NULL);
  scripts_->ui()->RegisterActionLocation("help_menu", ui_->menu_help, NULL);
  scripts_->ui()->RegisterActionLocation("playlist_menu", ui_->menu_playlist, NULL);

  // Load theme
  StyleSheetLoader* css_loader = new StyleSheetLoader(this);
  css_loader->SetStyleSheet(this, ":mainwindow.css");

  // Load playlists
  playlists_->Init(library_->backend(), playlist_backend_, ui_->playlist_sequence);

  // We need to connect these global shortcuts here after the playlist have been initialized
  connect(global_shortcuts_, SIGNAL(CycleShuffleMode()), player_->playlists()->sequence(), SLOT(CycleShuffleMode()));
  connect(global_shortcuts_, SIGNAL(CycleRepeatMode()), player_->playlists()->sequence(), SLOT(CycleRepeatMode()));
  connect(player_->playlists()->sequence(), SIGNAL(RepeatModeChanged(PlaylistSequence::RepeatMode)), osd_, SLOT(RepeatModeChanged(PlaylistSequence::RepeatMode)));
  connect(player_->playlists()->sequence(), SIGNAL(ShuffleModeChanged(PlaylistSequence::ShuffleMode)), osd_, SLOT(ShuffleModeChanged(PlaylistSequence::ShuffleMode)));

  // Load settings
  settings_.beginGroup(kSettingsGroup);

  restoreGeometry(settings_.value("geometry").toByteArray());
  if (!ui_->splitter->restoreState(settings_.value("splitter_state").toByteArray())) {
    ui_->splitter->setSizes(QList<int>() << 300 << width() - 300);
  }
  ui_->tabs->SetCurrentIndex(settings_.value("current_tab", 0).toInt());
  FancyTabWidget::Mode default_mode = FancyTabWidget::Mode_LargeSidebar;
  ui_->tabs->SetMode(FancyTabWidget::Mode(settings_.value(
      "tab_mode", default_mode).toInt()));
  file_view_->SetPath(settings_.value("file_path", QDir::homePath()).toString());

  ReloadSettings();

#ifndef Q_OS_DARWIN
  StartupBehaviour behaviour =
      StartupBehaviour(settings_.value("startupbehaviour", Startup_Remember).toInt());
  bool hidden = settings_.value("hidden", false).toBool();

  switch (behaviour) {
    case Startup_AlwaysHide: hide(); break;
    case Startup_AlwaysShow: show(); break;
    case Startup_Remember:   setVisible(!hidden); break;
  }

  // Force the window to show in case somehow the config has tray and window set to hide
  if (hidden && !tray_icon_->IsVisible()) {
    settings_.setValue("hidden", false);
    show();
  }
#else  // Q_OS_DARWIN
  // Always show mainwindow on startup on OS X.
  show();
#endif

  QShortcut* close_window_shortcut = new QShortcut(this);
  close_window_shortcut->setKey(Qt::CTRL + Qt::Key_W);
  connect(close_window_shortcut, SIGNAL(activated()), SLOT(SetHiddenInTray()));

  library_->Init();
  library_->StartThreads();

#ifdef HAVE_WIIMOTEDEV
// http://code.google.com/p/clementine-player/issues/detail?id=670
// Switched position, mayby something is not ready ?

  wiimotedev_shortcuts_.reset(new WiimotedevShortcuts(osd_, this, player_));
#endif

  // If we support more languages this ifdef will need to be changed.
#ifdef HAVE_SCRIPTING_PYTHON
  scripts_->Init(ScriptManager::GlobalData(
      library_, library_view_->view(), player_, playlists_,
      task_manager_, settings_dialog_.get(), radio_model_));
  connect(ui_->action_script_manager, SIGNAL(triggered()), SLOT(ShowScriptDialog()));

  library_view_->view()->SetScriptManager(scripts_);
#else
  ui_->action_script_manager->setVisible(false);
#endif

  CheckFullRescanRevisions();
}

MainWindow::~MainWindow() {
  SaveGeometry();
  delete ui_;

  // It's important that the device manager is deleted before the database.
  // Deleting the database deletes all objects that have been created in its
  // thread, including some device library backends.
  delete devices_; devices_ = NULL;
}

void MainWindow::ReloadSettings() {
#ifndef Q_OS_DARWIN
  bool show_tray = settings_.value("showtray", true).toBool();

  tray_icon_->SetVisible(show_tray);
  if (!show_tray && !isVisible())
    show();
#endif

  QSettings s;
  s.beginGroup(kSettingsGroup);

  doubleclick_addmode_ = AddBehaviour(
      s.value("doubleclick_addmode", AddBehaviour_Append).toInt());
  doubleclick_playmode_ = PlayBehaviour(
      s.value("doubleclick_playmode", PlayBehaviour_IfStopped).toInt());
  menu_playmode_ = PlayBehaviour(
      s.value("menu_playmode", PlayBehaviour_IfStopped).toInt());
}

void MainWindow::ReloadAllSettings() {
  ReloadSettings();

  // Other settings
  library_->ReloadSettings();
  player_->ReloadSettings();
  osd_->ReloadSettings();
  library_view_->view()->ReloadSettings();
  song_info_view_->ReloadSettings();
  player_->engine()->ReloadSettings();
  ui_->playlist->view()->ReloadSettings();
  radio_model_->ReloadSettings();
#ifdef HAVE_WIIMOTEDEV
  wiimotedev_shortcuts_->ReloadSettings();
#endif
#ifdef HAVE_REMOTE
  remote_->ReloadSettings();
#endif
}

void MainWindow::MediaStopped() {
  setWindowTitle(QCoreApplication::applicationName());

  ui_->action_stop->setEnabled(false);
  ui_->action_stop_after_this_track->setEnabled(false);
  ui_->action_play_pause->setIcon(IconLoader::Load("media-playback-start"));
  ui_->action_play_pause->setText(tr("Play"));

  ui_->action_play_pause->setEnabled(true);

  ui_->action_ban->setEnabled(false);
  ui_->action_love->setEnabled(false);

  track_position_timer_->stop();
  ui_->track_slider->SetStopped();
  tray_icon_->SetProgress(0);
  tray_icon_->SetStopped();
}

void MainWindow::MediaPaused() {
  ui_->action_stop->setEnabled(true);
  ui_->action_stop_after_this_track->setEnabled(true);
  ui_->action_play_pause->setIcon(IconLoader::Load("media-playback-start"));
  ui_->action_play_pause->setText(tr("Play"));

  ui_->action_play_pause->setEnabled(true);

  track_position_timer_->stop();

  tray_icon_->SetPaused();
}

void MainWindow::MediaPlaying() {
  ui_->action_stop->setEnabled(true);
  ui_->action_stop_after_this_track->setEnabled(true);
  ui_->action_play_pause->setIcon(IconLoader::Load("media-playback-pause"));
  ui_->action_play_pause->setText(tr("Pause"));

  bool enable_play_pause = !(player_->GetCurrentItem()->options() & PlaylistItem::PauseDisabled);
  ui_->action_play_pause->setEnabled(enable_play_pause);

#ifdef HAVE_LIBLASTFM
  bool is_lastfm = (player_->GetCurrentItem()->options() & PlaylistItem::LastFMControls);
  LastFMService* lastfm = RadioModel::Service<LastFMService>();
  bool enable_ban = lastfm->IsScrobblingEnabled() && is_lastfm;
  bool enable_love = lastfm->IsScrobblingEnabled();

  ui_->action_ban->setEnabled(enable_ban);
  ui_->action_love->setEnabled(enable_love);

  tray_icon_->SetPlaying(enable_play_pause, enable_ban, enable_love);

  ui_->track_slider->SetCanSeek(!is_lastfm);
#else
  ui_->track_slider->SetCanSeek(true);
  tray_icon_->SetPlaying(enable_play_pause);
#endif

  track_position_timer_->start();
  UpdateTrackPosition();
}

void MainWindow::VolumeChanged(int volume) {
  ui_->action_mute->setChecked(!volume);
}

void MainWindow::SongChanged(const Song& song) {
  setWindowTitle(song.PrettyTitleWithArtist());
}

void MainWindow::TrackSkipped(PlaylistItemPtr item) {
  // If it was a library item then we have to increment its skipped count in
  // the database.
  if (item && item->IsLocalLibraryItem() &&
      item->Metadata().id() != -1 && !playlists_->active()->has_scrobbled()) {
    Song song = item->Metadata();
    const qint64 position = player_->engine()->position_nanosec();
    const qint64 length = player_->engine()->length_nanosec();
    const float percentage = (length == 0 ? 1 : float(position) / length);

    library_->backend()->IncrementSkipCountAsync(song.id(), percentage);
  }
}

#ifdef HAVE_LIBLASTFM
void MainWindow::ScrobblingEnabledChanged(bool value) {
  if (!player_->GetState() == Engine::Idle)
    return;

  bool is_lastfm = (player_->GetCurrentItem()->options() & PlaylistItem::LastFMControls);
  ui_->action_ban->setEnabled(value && is_lastfm);
  ui_->action_love->setEnabled(value);
}
#endif

void MainWindow::LastFMButtonVisibilityChanged(bool value) {
  ui_->action_ban->setVisible(value);
  ui_->action_love->setVisible(value);
  ui_->last_fm_controls->setVisible(value);
}

void MainWindow::resizeEvent(QResizeEvent*) {
  SaveGeometry();
}

void MainWindow::SaveGeometry() {
  settings_.setValue("geometry", saveGeometry());
  settings_.setValue("splitter_state", ui_->splitter->saveState());
  settings_.setValue("current_tab", ui_->tabs->current_index());
  settings_.setValue("tab_mode", ui_->tabs->mode());
}

void MainWindow::PlayIndex(const QModelIndex& index) {
  if (!index.isValid())
    return;

  int row = index.row();
  if (index.model() == playlists_->current()->proxy()) {
    // The index was in the proxy model (might've been filtered), so we need
    // to get the actual row in the source model.
    row = playlists_->current()->proxy()->mapToSource(index).row();
  }

  playlists_->SetActiveToCurrent();
  player_->PlayAt(row, Engine::Manual, true);
}

void MainWindow::VolumeWheelEvent(int delta) {
  ui_->volume->setValue(ui_->volume->value() + delta / 30);
}

void MainWindow::ToggleShowHide() {
  if (settings_.value("hidden").toBool()) {
    show();
    SetHiddenInTray(false);
  } else if (isActiveWindow()) {
    hide();
    setWindowState((windowState() & ~Qt::WindowMinimized) | Qt::WindowActive);
    SetHiddenInTray(true);
  } else if (isMinimized()) {
    hide();
    setWindowState((windowState() & ~Qt::WindowMinimized) | Qt::WindowActive);
    SetHiddenInTray(false);
  } else if (!isVisible())
  {
    show();
    activateWindow();
  } else {
    // Window is not hidden but does not have focus; bring it to front.
    activateWindow();
    raise();
  }
}

void MainWindow::StopAfterCurrent() {
  playlists_->current()->StopAfter(playlists_->current()->current_row());
}

void MainWindow::closeEvent(QCloseEvent* event) {
  QSettings s;
  s.beginGroup(kSettingsGroup);

  bool keep_running = s.value("keeprunning", tray_icon_->IsVisible()).toBool();

  if (keep_running && event->spontaneous()) {
    event->ignore();
    SetHiddenInTray(true);
  } else {
    QApplication::quit();
  }
}

void MainWindow::SetHiddenInTray(bool hidden) {
  settings_.setValue("hidden", hidden);

  // Some window managers don't remember maximized state between calls to
  // hide() and show(), so we have to remember it ourself.
  if (hidden) {
    was_maximized_ = isMaximized();
    hide();
  } else {
    if (was_maximized_)
      showMaximized();
    else
      show();
  }
}

void MainWindow::FilePathChanged(const QString& path) {
  settings_.setValue("file_path", path);
}

void MainWindow::UpdateTrackPosition() {
  // Track position in seconds
  PlaylistItemPtr item(player_->GetCurrentItem());
  const int position = std::floor(
      float(player_->engine()->position_nanosec()) / kNsecPerSec + 0.5);
  const int length = item->Metadata().length_nanosec() / kNsecPerSec;
  const int scrobble_point = playlists_->active()->scrobble_point_nanosec() / kNsecPerSec;

  if (length <= 0) {
    // Probably a stream that we don't know the length of
    ui_->track_slider->SetStopped();
    tray_icon_->SetProgress(0);
    return;
  }

  // Time to scrobble?
  if (!playlists_->active()->has_scrobbled() && position >= scrobble_point) {
#ifdef HAVE_LIBLASTFM
    radio_model_->RadioModel::Service<LastFMService>()->Scrobble();
#endif
    playlists_->active()->set_scrobbled(true);

    // Update the play count for the song if it's from the library
    if (item->IsLocalLibraryItem() && item->Metadata().id() != -1) {
      library_->backend()->IncrementPlayCountAsync(item->Metadata().id());
    }
  }

  // Update the slider
  ui_->track_slider->SetValue(position, length);

  // Update the tray icon every 10 seconds
  if (position % 10 == 0) {
    tray_icon_->SetProgress(double(position) / length * 100);
  }
}

#ifdef HAVE_LIBLASTFM
void MainWindow::Love() {
  RadioModel::Service<LastFMService>()->Love();
  ui_->action_love->setEnabled(false);
}
#endif

void MainWindow::ApplyAddBehaviour(MainWindow::AddBehaviour b, MimeData* data) const {
  switch (b) {
  case AddBehaviour_Append:
    data->clear_first_ = false;
    data->enqueue_now_ = false;
    break;

  case AddBehaviour_Enqueue:
    data->clear_first_ = false;
    data->enqueue_now_ = true;
    break;

  case AddBehaviour_Load:
    data->clear_first_ = true;
    data->enqueue_now_ = false;
    break;

  case AddBehaviour_OpenInNew:
    data->open_in_new_playlist_ = true;
    break;
  }
}

void MainWindow::ApplyPlayBehaviour(MainWindow::PlayBehaviour b, MimeData* data) const {
  switch (b) {
  case PlayBehaviour_Always:
    data->play_now_ = true;
    break;

  case PlayBehaviour_Never:
    data->play_now_ = false;
    break;

  case PlayBehaviour_IfStopped:
    data->play_now_ = !(player_->GetState() == Engine::Playing);
    break;
  }
}

void MainWindow::AddToPlaylist(QMimeData* data) {
  if (!data)
    return;

  if (MimeData* mime_data = qobject_cast<MimeData*>(data)) {
    // Should we replace the flags with the user's preference?
    if (mime_data->from_doubleclick_) {
      ApplyAddBehaviour(doubleclick_addmode_, mime_data);
      ApplyPlayBehaviour(doubleclick_playmode_, mime_data);
    } else {
      ApplyPlayBehaviour(menu_playmode_, mime_data);
    }

    // Should we create a new playlist for the songs?
    if(mime_data->open_in_new_playlist_) {
      playlists_->New(mime_data->get_name_for_new_playlist());
    }
  }

  playlists_->current()->dropMimeData(data, Qt::CopyAction, -1, 0, QModelIndex());
  delete data;
}

void MainWindow::AddToPlaylist(QAction* action) {
  int destination = action->data().toInt();
  PlaylistItemList items;

  //get the selected playlist items
  foreach (const QModelIndex& index, ui_->playlist->view()->selectionModel()->selection().indexes()) {
    if (index.column() != 0)
      continue;
    int row = playlists_->current()->proxy()->mapToSource(index).row();
    items << playlists_->current()->item_at(row);
  }

  //we're creating a new playlist
  if (destination == -1) {
    //save the current playlist to reactivate it
    int current_id = playlists_->current_id();
    //ask for the name    
    playlists_->New(ui_->playlist->PromptForPlaylistName());
    if (playlists_->current()->id() != current_id) {
      //I'm sure the new playlist was created and is selected, so I can just insert items
      playlists_->current()->InsertItems(items);
      //set back the current playlist
      playlists_->SetCurrentPlaylist(current_id);
    }
  }
  else {
    //we're inserting in a existing playlist
    playlists_->playlist(destination)->InsertItems(items);
  }
}

void MainWindow::PlaylistRightClick(const QPoint& global_pos, const QModelIndex& index) {
  QModelIndex source_index = playlists_->current()->proxy()->mapToSource(index);
  playlist_menu_index_ = source_index;

  // Is this song currently playing?
  if (playlists_->current()->current_row() == source_index.row() && player_->GetState() == Engine::Playing) {
    playlist_play_pause_->setText(tr("Pause"));
    playlist_play_pause_->setIcon(IconLoader::Load("media-playback-pause"));
  } else {
    playlist_play_pause_->setText(tr("Play"));
    playlist_play_pause_->setIcon(IconLoader::Load("media-playback-start"));
  }

  // Are we allowed to pause?
  if (index.isValid()) {
    playlist_play_pause_->setEnabled(
        playlists_->current()->current_row() != source_index.row() ||
        ! (playlists_->current()->item_at(source_index.row())->options() & PlaylistItem::PauseDisabled));
  } else {
    playlist_play_pause_->setEnabled(false);
  }

  playlist_stop_after_->setEnabled(index.isValid());

  // Are any of the selected songs editable or queued?
  QModelIndexList selection = ui_->playlist->view()->selectionModel()->selection().indexes();
  bool cue_selected = false;
  int editable = 0;
  int streams = 0;
  int in_queue = 0;
  int not_in_queue = 0;
  foreach (const QModelIndex& index, selection) {
    if (index.column() != 0)
      continue;

    PlaylistItemPtr item = playlists_->current()->item_at(index.row());
    if(item->Metadata().has_cue()) {
      cue_selected = true;
    } else if (item->Metadata().IsEditable()) {
      editable++;
    }

    if(item->Metadata().is_stream()) {
      streams++;
    }

    if (index.data(Playlist::Role_QueuePosition).toInt() == -1)
      not_in_queue ++;
    else
      in_queue ++;
  }

  int all = not_in_queue + in_queue;

  // this is available when we have one or many files and at least one of
  // those is not CUE related
  ui_->action_edit_track->setEnabled(editable);
  ui_->action_edit_track->setVisible(editable);
  ui_->action_auto_complete_tags->setEnabled(editable);
  ui_->action_auto_complete_tags->setVisible(editable);
  // the rest of the read / write actions work only when there are no CUEs
  // involved
  if(cue_selected)
    editable = 0;

  // no 'show in browser' action if only streams are selected
  playlist_open_in_browser_->setVisible(streams != all);

  bool track_column = (index.column() == Playlist::Column_Track);
  ui_->action_renumber_tracks->setVisible(editable >= 2 && track_column);
  ui_->action_selection_set_value->setVisible(editable >= 2 && !track_column);
  ui_->action_edit_value->setVisible(editable);
  ui_->action_remove_from_playlist->setEnabled(!selection.isEmpty());

  playlist_copy_to_library_->setVisible(false);
  playlist_move_to_library_->setVisible(false);
  playlist_organise_->setVisible(false);
  playlist_delete_->setVisible(false);
  playlist_copy_to_device_->setVisible(false);

  if (in_queue == 1 && not_in_queue == 0)
    playlist_queue_->setText(tr("Dequeue track"));
  else if (in_queue > 1 && not_in_queue == 0)
    playlist_queue_->setText(tr("Dequeue selected tracks"));
  else if (in_queue == 0 && not_in_queue == 1)
    playlist_queue_->setText(tr("Queue track"));
  else if (in_queue == 0 && not_in_queue > 1)
    playlist_queue_->setText(tr("Queue selected tracks"));
  else
    playlist_queue_->setText(tr("Toggle queue status"));

  if (not_in_queue == 0)
    playlist_queue_->setIcon(IconLoader::Load("go-previous"));
  else
    playlist_queue_->setIcon(IconLoader::Load("go-next"));

  if (!index.isValid()) {
    ui_->action_selection_set_value->setVisible(false);
    ui_->action_edit_value->setVisible(false);
  } else {
    Playlist::Column column = (Playlist::Column)index.column();
    bool column_is_editable = Playlist::column_is_editable(column)
                              && editable;

    ui_->action_selection_set_value->setVisible(
        ui_->action_selection_set_value->isVisible() && column_is_editable);
    ui_->action_edit_value->setVisible(
        ui_->action_edit_value->isVisible() && column_is_editable);

    QString column_name = Playlist::column_name(column);
    QString column_value = playlists_->current()->data(source_index).toString();
    if (column_value.length() > 25)
      column_value = column_value.left(25) + "...";

    ui_->action_selection_set_value->setText(tr("Set %1 to \"%2\"...")
             .arg(column_name.toLower()).arg(column_value));
    ui_->action_edit_value->setText(tr("Edit tag \"%1\"...").arg(column_name));

    // Is it a library item?
    PlaylistItemPtr item = playlists_->current()->item_at(source_index.row());
    if (item->IsLocalLibraryItem() && item->Metadata().id() != -1) {
      playlist_organise_->setVisible(editable);
    } else {
      playlist_copy_to_library_->setVisible(editable);
      playlist_move_to_library_->setVisible(editable);
    }

    playlist_delete_->setVisible(editable);
    playlist_copy_to_device_->setVisible(editable);
  }

  //if it isn't the first time we right click, we need to remove the menu previously created
  if (playlist_add_to_another_ != NULL) {
    playlist_menu_->removeAction(playlist_add_to_another_);
    delete playlist_add_to_another_;
  }

  //create the playlist submenu
  QMenu* add_to_another_menu = new QMenu(tr("Add to another playlist"), this);
  add_to_another_menu->setIcon((IconLoader::Load("add")));

  PlaylistBackend::Playlist playlist;
  foreach (playlist, playlist_backend_->GetAllPlaylists()) {
    //don't add the current playlist
    if (playlist.id != playlists_->current()->id()) {
      QAction* existing_playlist = new QAction(this);
      existing_playlist->setText(playlist.name);
      existing_playlist->setData(playlist.id);
      add_to_another_menu->addAction(existing_playlist);
    }
  }

  add_to_another_menu->addSeparator();
  //add to a new playlist
  QAction* new_playlist = new QAction(this);
  new_playlist->setText(tr("New playlist"));
  new_playlist->setData(-1); //fake id
  add_to_another_menu->addAction(new_playlist);
  playlist_add_to_another_ = playlist_menu_->insertMenu(ui_->action_remove_from_playlist,
                                                        add_to_another_menu);

  connect(add_to_another_menu, SIGNAL(triggered(QAction*)), SLOT(AddToPlaylist(QAction*)));

  playlist_menu_->popup(global_pos);
}

void MainWindow::PlaylistPlay() {
  if (playlists_->current()->current_row() == playlist_menu_index_.row()) {
    player_->PlayPause();
  } else {
    PlayIndex(playlist_menu_index_);
  }
}

void MainWindow::PlaylistStopAfter() {
  playlists_->current()->StopAfter(playlist_menu_index_.row());
}

void MainWindow::EditTracks() {
  SongList songs;
  PlaylistItemList items;

  foreach (const QModelIndex& index,
           ui_->playlist->view()->selectionModel()->selection().indexes()) {
    if (index.column() != 0)
      continue;
    int row = playlists_->current()->proxy()->mapToSource(index).row();
    PlaylistItemPtr item(playlists_->current()->item_at(row));
    Song song = item->Metadata();

    if (song.IsEditable()) {
      songs << song;
      items << item;
    }
  }

  EnsureEditTagDialogCreated();
  edit_tag_dialog_->SetSongs(songs, items);
  edit_tag_dialog_->SetTagCompleter(library_->model()->backend());
  edit_tag_dialog_->show();
}

void MainWindow::EditTagDialogAccepted() {
  foreach (PlaylistItemPtr item, edit_tag_dialog_->playlist_items()) {
    item->Reload();
  }

  // This is really lame but we don't know what rows have changed
  ui_->playlist->view()->update();
}

void MainWindow::RenumberTracks() {
  QModelIndexList indexes=ui_->playlist->view()->selectionModel()->selection().indexes();
  int track=1;

  // Get the index list in order
  qStableSort(indexes);

  // if first selected song has a track number set, start from that offset
  if (indexes.size()) {
    Song first_song=playlists_->current()->item_at(indexes[0].row())->Metadata();
    if (int first_track = first_song.track())
      track = first_track;
  }

  foreach (const QModelIndex& index, indexes) {
    if (index.column() != 0)
      continue;

    int row = playlists_->current()->proxy()->mapToSource(index).row();
    Song song = playlists_->current()->item_at(row)->Metadata();

    if (song.IsEditable()) {
      song.set_track(track);
      QFuture<bool> future = song.BackgroundSave();
      ModelFutureWatcher<bool>* watcher = new ModelFutureWatcher<bool>(index, this);
      watcher->setFuture(future);
      connect(watcher, SIGNAL(finished()), SLOT(SongSaveComplete()));
    }
    track++;
  }
}

void MainWindow::SongSaveComplete() {
  ModelFutureWatcher<bool>* watcher = static_cast<ModelFutureWatcher<bool>*>(sender());
  watcher->deleteLater();
  if (watcher->index().isValid()) {
    playlists_->current()->ReloadItems(QList<int>() << watcher->index().row());
  }
}

void MainWindow::SelectionSetValue() {
  Playlist::Column column = (Playlist::Column)playlist_menu_index_.column();
  QVariant column_value = playlists_->current()->data(playlist_menu_index_);

  QModelIndexList indexes =
      ui_->playlist->view()->selectionModel()->selection().indexes();
  foreach (const QModelIndex& index, indexes) {
    if (index.column() != 0)
      continue;

    int row = playlists_->current()->proxy()->mapToSource(index).row();
    Song song = playlists_->current()->item_at(row)->Metadata();

    if (Playlist::set_column_value(song, column, column_value)) {
      QFuture<bool> future = song.BackgroundSave();
      ModelFutureWatcher<bool>* watcher = new ModelFutureWatcher<bool>(index, this);
      watcher->setFuture(future);
      connect(watcher, SIGNAL(finished()), SLOT(SongSaveComplete()));
    }
  }
}

void MainWindow::EditValue() {
  QModelIndex current = ui_->playlist->view()->currentIndex();
  if (!current.isValid())
    return;

  // Edit the last column that was right-clicked on.  If nothing's ever been
  // right clicked then look for the first editable column.
  int column = playlist_menu_index_.column();
  if (column == -1) {
    for (int i=0 ; i<ui_->playlist->view()->model()->columnCount() ; ++i) {
      if (ui_->playlist->view()->isColumnHidden(i))
        continue;
      if (!Playlist::column_is_editable(Playlist::Column(i)))
        continue;
      column = i;
      break;
    }
  }

  ui_->playlist->view()->edit(current.sibling(current.row(), column));
}

void MainWindow::AddFile() {
  // Last used directory
  QString directory = settings_.value("add_media_path", QDir::currentPath()).toString();

  PlaylistParser parser(library_->backend());

  // Show dialog
  QStringList file_names = QFileDialog::getOpenFileNames(
      this, tr("Add media"), directory,
      QString("%1;;%2;;%3").arg(tr(kMusicFilterSpec), parser.filters(),
                                tr(kAllFilesFilterSpec)));
  if (file_names.isEmpty())
    return;

  // Save last used directory
  settings_.setValue("add_media_path", file_names[0]);

  // Convert to URLs
  QList<QUrl> urls;
  foreach (const QString& path, file_names) {
    urls << QUrl::fromLocalFile(QFileInfo(path).canonicalFilePath());
  }

  MimeData* data = new MimeData;
  data->setUrls(urls);
  AddToPlaylist(data);
}

void MainWindow::AddFolder() {
  // Last used directory
  QString directory = settings_.value("add_folder_path", QDir::currentPath()).toString();

  // Show dialog
  directory = QFileDialog::getExistingDirectory(this, tr("Add folder"), directory);
  if (directory.isEmpty())
    return;

  // Save last used directory
  settings_.setValue("add_folder_path", directory);

  // Add media
  MimeData* data = new MimeData;
  data->setUrls(QList<QUrl>() << QUrl::fromLocalFile(QFileInfo(directory).canonicalFilePath()));
  AddToPlaylist(data);
}

void MainWindow::AddStream() {
  if (!add_stream_dialog_) {
    add_stream_dialog_.reset(new AddStreamDialog);
    connect(add_stream_dialog_.get(), SIGNAL(accepted()), SLOT(AddStreamAccepted()));

    add_stream_dialog_->set_add_on_accept(RadioModel::Service<SavedRadio>());
  }

  add_stream_dialog_->show();
}

void MainWindow::AddStreamAccepted() {
  MimeData* data = new MimeData;
  data->setUrls(QList<QUrl>() << add_stream_dialog_->url());
  AddToPlaylist(data);
}

void MainWindow::PlaylistRemoveCurrent() {
  ui_->playlist->view()->RemoveSelected();
}

void MainWindow::PlaylistEditFinished(const QModelIndex& index) {
  if (index == playlist_menu_index_)
    SelectionSetValue();
}

void MainWindow::CommandlineOptionsReceived(const QByteArray& serialized_options) {
  if (serialized_options == "wake up!") {
    // Old versions of Clementine sent this - just ignore it
    return;
  }

  CommandlineOptions options;
  options.Load(serialized_options);

  if (options.is_empty()) {
    show();
    activateWindow();
  }
  else
    CommandlineOptionsReceived(options);
}

void MainWindow::CommandlineOptionsReceived(const CommandlineOptions &options) {
  switch (options.player_action()) {
    case CommandlineOptions::Player_Play:
      player_->Play();
      break;
    case CommandlineOptions::Player_PlayPause:
      player_->PlayPause();
      break;
    case CommandlineOptions::Player_Pause:
      player_->Pause();
      break;
    case CommandlineOptions::Player_Stop:
      player_->Stop();
      break;
    case CommandlineOptions::Player_Previous:
      player_->Previous();
      break;
    case CommandlineOptions::Player_Next:
      player_->Next();
      break;

    case CommandlineOptions::Player_None:
      break;
  }

  switch (options.url_list_action()) {
    case CommandlineOptions::UrlList_Load:
      playlists_->ClearCurrent();

      // fallthrough
    case CommandlineOptions::UrlList_Append: {
      MimeData* data = new MimeData;
      data->setUrls(options.urls());
      AddToPlaylist(data);
      break;
    }
  }

  if (options.set_volume() != -1)
    player_->SetVolume(options.set_volume());

  if (options.volume_modifier() != 0)
    player_->SetVolume(player_->GetVolume() + options.volume_modifier());

  if (options.seek_to() != -1)
    player_->SeekTo(options.seek_to() * kNsecPerSec);
  else if (options.seek_by() != 0)
    player_->SeekTo(player_->engine()->position_nanosec() + options.seek_by() * kNsecPerSec);

  if (options.play_track_at() != -1)
    player_->PlayAt(options.play_track_at(), Engine::Manual, true);

  if (options.show_osd())
    player_->ShowOSD();
}

void MainWindow::ForceShowOSD(const Song &song) {
  osd_->ForceShowNextNotification();
  osd_->SongChanged(song);
}

void MainWindow::Activate() {
  show();
}

bool MainWindow::LoadUrl(const QString& url) {
  if (!QFile::exists(url))
    return false;

  MimeData* data = new MimeData;
  data->setUrls(QList<QUrl>() << QUrl::fromLocalFile(url));
  AddToPlaylist(data);

  return true;
}

void MainWindow::CheckForUpdates() {
#if defined(Q_OS_DARWIN)
  mac::CheckForUpdates();
#endif
}

void MainWindow::PlaylistUndoRedoChanged(QAction *undo, QAction *redo) {
  playlist_menu_->insertAction(playlist_undoredo_, undo);
  playlist_menu_->insertAction(playlist_undoredo_, redo);
}

void MainWindow::ShowLibraryConfig() {
  EnsureSettingsDialogCreated();
  settings_dialog_->OpenAtPage(SettingsDialog::Page_Library);
}

void MainWindow::TaskCountChanged(int count) {
  if (count == 0) {
    ui_->status_bar_stack->setCurrentWidget(ui_->playlist_summary_page);
  } else {
    ui_->status_bar_stack->setCurrentWidget(ui_->multi_loading_indicator);
  }
}

void MainWindow::NowPlayingWidgetPositionChanged(bool above_status_bar) {
  if (above_status_bar) {
    ui_->status_bar->setParent(ui_->centralWidget);
  } else {
    ui_->status_bar->setParent(ui_->player_controls_container);
  }

  ui_->status_bar->parentWidget()->layout()->addWidget(ui_->status_bar);
  ui_->status_bar->show();
}

void MainWindow::CopyFilesToLibrary(const QList<QUrl> &urls) {
  organise_dialog_->SetDestinationModel(library_->model()->directory_model());
  organise_dialog_->SetUrls(urls);
  organise_dialog_->SetCopy(true);
  organise_dialog_->show();
}

void MainWindow::MoveFilesToLibrary(const QList<QUrl> &urls) {
  organise_dialog_->SetDestinationModel(library_->model()->directory_model());
  organise_dialog_->SetUrls(urls);
  organise_dialog_->SetCopy(false);
  organise_dialog_->show();
}

void MainWindow::CopyFilesToDevice(const QList<QUrl> &urls) {
  organise_dialog_->SetDestinationModel(devices_->connected_devices_model(), true);
  organise_dialog_->SetCopy(true);
  if (organise_dialog_->SetUrls(urls))
    organise_dialog_->show();
  else {
    QMessageBox::warning(this, tr("Error"),
        tr("None of the selected songs were suitable for copying to a device"));
  }
}

void MainWindow::PlaylistCopyToLibrary() {
  PlaylistOrganiseSelected(true);
}

void MainWindow::PlaylistMoveToLibrary() {
  PlaylistOrganiseSelected(false);
}

void MainWindow::PlaylistOrganiseSelected(bool copy) {
  QModelIndexList proxy_indexes = ui_->playlist->view()->selectionModel()->selectedRows();
  SongList songs;

  foreach (const QModelIndex& proxy_index, proxy_indexes) {
    QModelIndex index = playlists_->current()->proxy()->mapToSource(proxy_index);

    songs << playlists_->current()->item_at(index.row())->Metadata();
  }

  organise_dialog_->SetDestinationModel(library_->model()->directory_model());
  organise_dialog_->SetSongs(songs);
  organise_dialog_->SetCopy(copy);
  organise_dialog_->show();
}

void MainWindow::PlaylistDelete() {
  // Note: copied from LibraryView::Delete

  if (QMessageBox::question(this, tr("Delete files"),
        tr("These files will be deleted from disk, are you sure you want to continue?"),
        QMessageBox::Yes, QMessageBox::Cancel) != QMessageBox::Yes)
    return;

  // We can cheat and always take the storage of the first directory, since
  // they'll all be FilesystemMusicStorage in a library and deleting doesn't
  // check the actual directory.
  boost::shared_ptr<MusicStorage> storage =
      library_->model()->directory_model()->index(0, 0).data(MusicStorage::Role_Storage)
      .value<boost::shared_ptr<MusicStorage> >();

  // Get selected songs
  SongList selected_songs;
  QModelIndexList proxy_indexes = ui_->playlist->view()->selectionModel()->selectedRows();
  foreach (const QModelIndex& proxy_index, proxy_indexes) {
    QModelIndex index = playlists_->current()->proxy()->mapToSource(proxy_index);
    selected_songs << playlists_->current()->item_at(index.row())->Metadata();
  }

  ui_->playlist->view()->RemoveSelected();

  DeleteFiles* delete_files = new DeleteFiles(task_manager_, storage);
  connect(delete_files, SIGNAL(Finished(SongList)), SLOT(DeleteFinished(SongList)));
  delete_files->Start(selected_songs);
}

void MainWindow::PlaylistOpenInBrowser() {
  QStringList filenames;
  QModelIndexList proxy_indexes = ui_->playlist->view()->selectionModel()->selectedRows();

  foreach (const QModelIndex& proxy_index, proxy_indexes) {
    const QModelIndex index = playlists_->current()->proxy()->mapToSource(proxy_index);
    filenames << index.sibling(index.row(), Playlist::Column_Filename).data().toString();
  }

  Utilities::OpenInFileBrowser(filenames);
}

void MainWindow::DeleteFinished(const SongList& songs_with_errors) {
  if (songs_with_errors.isEmpty())
    return;

  OrganiseErrorDialog* dialog = new OrganiseErrorDialog(this);
  dialog->Show(OrganiseErrorDialog::Type_Delete, songs_with_errors);
  // It deletes itself when the user closes it
}

void MainWindow::PlaylistQueue() {
  QModelIndexList indexes;
  foreach (const QModelIndex& proxy_index,
           ui_->playlist->view()->selectionModel()->selectedRows()) {
    indexes << playlists_->current()->proxy()->mapToSource(proxy_index);
  }

  playlists_->current()->queue()->ToggleTracks(indexes);
}

void MainWindow::PlaylistCopyToDevice() {
  QModelIndexList proxy_indexes = ui_->playlist->view()->selectionModel()->selectedRows();
  SongList songs;

  foreach (const QModelIndex& proxy_index, proxy_indexes) {
    QModelIndex index = playlists_->current()->proxy()->mapToSource(proxy_index);

    songs << playlists_->current()->item_at(index.row())->Metadata();
  }

  organise_dialog_->SetDestinationModel(devices_->connected_devices_model(), true);
  organise_dialog_->SetCopy(true);
  if (organise_dialog_->SetSongs(songs))
    organise_dialog_->show();
  else {
    QMessageBox::warning(this, tr("Error"),
        tr("None of the selected songs were suitable for copying to a device"));
  }
}

void MainWindow::ChangeLibraryQueryMode(QAction* action) {
  if(action == library_show_duplicates_) {
    library_view_->filter()->SetQueryMode(QueryOptions::QueryMode_Duplicates);
  } else if (action == library_show_untagged_) {
    library_view_->filter()->SetQueryMode(QueryOptions::QueryMode_Untagged);
  } else {
    library_view_->filter()->SetQueryMode(QueryOptions::QueryMode_All);
  }
}

#ifdef HAVE_LIBLASTFM
void MainWindow::ShowCoverManager() {
  if (!cover_manager_) {
    cover_manager_.reset(new AlbumCoverManager(library_->backend()));
    cover_manager_->Init();

    // Cover manager connections
    connect(cover_manager_.get(), SIGNAL(AddToPlaylist(QMimeData*)), SLOT(AddToPlaylist(QMimeData*)));
  }

  cover_manager_->show();
}
#endif

void MainWindow::EnsureSettingsDialogCreated() {
  if (settings_dialog_)
    return;

  settings_dialog_.reset(new SettingsDialog(background_streams_));
  settings_dialog_->SetLibraryDirectoryModel(library_->model()->directory_model());

  settings_dialog_->SetGstEngine(qobject_cast<GstEngine*>(player_->engine()));

  settings_dialog_->SetGlobalShortcutManager(global_shortcuts_);
  settings_dialog_->SetSongInfoView(song_info_view_);

  // Settings
  connect(settings_dialog_.get(), SIGNAL(accepted()), SLOT(ReloadAllSettings()));

#ifdef HAVE_WIIMOTEDEV
  connect(settings_dialog_.get(), SIGNAL(SetWiimotedevInterfaceActived(bool)), wiimotedev_shortcuts_.get(), SLOT(SetWiimotedevInterfaceActived(bool)));
#endif
}

void MainWindow::OpenSettingsDialog() {
  EnsureSettingsDialogCreated();
  settings_dialog_->show();
}

void MainWindow::OpenSettingsDialogAtPage(SettingsDialog::Page page) {
  EnsureSettingsDialogCreated();
  settings_dialog_->OpenAtPage(page);
}

void MainWindow::EnsureEditTagDialogCreated() {
  if (edit_tag_dialog_)
    return;

  edit_tag_dialog_.reset(new EditTagDialog);
  connect(edit_tag_dialog_.get(), SIGNAL(accepted()), SLOT(EditTagDialogAccepted()));
  connect(edit_tag_dialog_.get(), SIGNAL(Error(QString)), SLOT(ShowErrorDialog(QString)));
}

void MainWindow::ShowAboutDialog() {
  if (!about_dialog_) {
    about_dialog_.reset(new About);
  }

  about_dialog_->show();
}

void MainWindow::ShowTranscodeDialog() {
  if (!transcode_dialog_) {
    transcode_dialog_.reset(new TranscodeDialog);
  }
  transcode_dialog_->show();
}

void MainWindow::ShowErrorDialog(const QString& message) {
  if (!error_dialog_) {
    error_dialog_.reset(new ErrorDialog);
  }
  error_dialog_->ShowMessage(message);
}

void MainWindow::CheckFullRescanRevisions() {
  int from = database_->Worker()->startup_schema_version();
  int to = database_->Worker()->current_schema_version();

  // if we're restoring DB from scratch or nothing has
  // changed, do nothing
  if(from == 0 || from == to) {
    return;
  }

  // collect all reasons
  QSet<QString> reasons;
  for(int i = from; i <= to; i++) {
    QString reason = library_->full_rescan_reason(i);

    if(!reason.isEmpty()) {
      reasons.insert(reason);
    }
  }

  // if we have any...
  if(!reasons.isEmpty()) {
    QString message = tr("The version of Clementine you've just updated to requires a full library rescan "
                         "because of the new features listed below:") + "<ul>";
    foreach(const QString& reason, reasons) {
      message += ("<li>" + reason + "</li>");
    }
    message += "</ul>" + tr("Would you like to run a full rescan right now?");

    if(QMessageBox::question(this, tr("Library rescan notice"),
                             message, QMessageBox::Yes, QMessageBox::No) == QMessageBox::Yes) {
      library_->FullScan();
    }
  }
}

void MainWindow::ShowQueueManager() {
  if (!queue_manager_) {
    queue_manager_.reset(new QueueManager);
    queue_manager_->SetPlaylistManager(playlists_);
  }
  queue_manager_->show();
}

void MainWindow::ShowVisualisations() {
#ifdef ENABLE_VISUALISATIONS
  if (!visualisation_) {
    visualisation_.reset(new VisualisationContainer);

    visualisation_->SetActions(ui_->action_previous_track, ui_->action_play_pause,
                               ui_->action_stop, ui_->action_next_track);
    connect(player_, SIGNAL(Stopped()), visualisation_.get(), SLOT(Stopped()));
    connect(player_, SIGNAL(ForceShowOSD(Song)), visualisation_.get(), SLOT(SongMetadataChanged(Song)));
    connect(playlists_, SIGNAL(CurrentSongChanged(Song)), visualisation_.get(), SLOT(SongMetadataChanged(Song)));

    visualisation_->SetEngine(qobject_cast<GstEngine*>(player_->engine()));
  }

  visualisation_->show();
#endif // ENABLE_VISUALISATIONS
}

void MainWindow::ConnectInfoView(SongInfoBase* view) {
  connect(playlists_, SIGNAL(CurrentSongChanged(Song)), view, SLOT(SongChanged(Song)));
  connect(player_, SIGNAL(PlaylistFinished()), view, SLOT(SongFinished()));
  connect(player_, SIGNAL(Stopped()), view, SLOT(SongFinished()));

  connect(view, SIGNAL(ShowSettingsDialog()), SLOT(ShowSongInfoConfig()));
  connect(view, SIGNAL(AddToPlaylist(QMimeData*)), SLOT(AddToPlaylist(QMimeData*)));
}

void MainWindow::AddSongInfoGenerator(smart_playlists::GeneratorPtr gen) {
  if (!gen)
    return;
  gen->set_library(library_->backend());

  AddToPlaylist(new smart_playlists::GeneratorMimeData(gen));
}

void MainWindow::ShowSongInfoConfig() {
  OpenSettingsDialogAtPage(SettingsDialog::Page_SongInformation);
}

void MainWindow::PlaylistViewSelectionModelChanged() {
  connect(ui_->playlist->view()->selectionModel(),
          SIGNAL(currentChanged(QModelIndex,QModelIndex)),
          SLOT(PlaylistCurrentChanged(QModelIndex)));
}

void MainWindow::PlaylistCurrentChanged(const QModelIndex& proxy_current) {
  const QModelIndex source_current =
      playlists_->current()->proxy()->mapToSource(proxy_current);

  // If the user moves the current index using the keyboard and then presses
  // F2, we don't want that editing the last column that was right clicked on.
  if (source_current != playlist_menu_index_)
    playlist_menu_index_ = QModelIndex();
}

void MainWindow::Raise() {
  show();
  activateWindow();
}

void MainWindow::ShowScriptDialog() {
  if (!script_dialog_) {
    script_dialog_.reset(new ScriptDialog);
    script_dialog_->SetManager(scripts_);
  }
  script_dialog_->show();
}

#ifdef Q_OS_WIN32
bool MainWindow::winEvent(MSG* msg, long*) {
  thumbbar_->HandleWinEvent(msg);
  return false;
}
#endif // Q_OS_WIN32

void MainWindow::Exit() {
  if(player_->engine()->is_fadeout_enabled()) {
    // To shut down the application when fadeout will be finished
    connect(player_->engine(), SIGNAL(FadeoutFinishedSignal()), qApp, SLOT(quit()));
    if(player_->GetState() == Engine::Playing) {
      player_->Stop();
      hide();
      tray_icon_->SetVisible(false);
      return; // Don't quit the application now: wait for the fadeout finished signal
    }
  }
  qApp->quit();
}

void MainWindow::AutoCompleteTags() {
  if (!Fingerprinter::GstreamerHasOfa()) {
    QMessageBox::warning(this, tr("Error"), tr("Your gstreamer installation is missing the 'ofa' plugin.  This is required for automatic tag fetching.  Try installing the 'gstreamer-plugins-bad' package."));
    return;
  }

  // Create the tag fetching stuff if it hasn't been already
  if (!tag_fetcher_) {
    tag_fetcher_.reset(new TagFetcher);
    track_selection_dialog_.reset(new TrackSelectionDialog);
    track_selection_dialog_->set_save_on_close(true);

    connect(tag_fetcher_.get(), SIGNAL(ResultAvailable(Song, SongList)),
            track_selection_dialog_.get(), SLOT(FetchTagFinished(Song, SongList)),
            Qt::QueuedConnection);
    connect(tag_fetcher_.get(), SIGNAL(Progress(Song,QString)),
            track_selection_dialog_.get(), SLOT(FetchTagProgress(Song,QString)));
    connect(track_selection_dialog_.get(), SIGNAL(accepted()),
            SLOT(AutoCompleteTagsAccepted()));
    connect(track_selection_dialog_.get(), SIGNAL(finished(int)),
            tag_fetcher_.get(), SLOT(Cancel()));
  }

  // Get the selected songs and start fetching tags for them
  SongList songs;
  autocomplete_tag_items_.clear();
  foreach (const QModelIndex& index,
           ui_->playlist->view()->selectionModel()->selection().indexes()) {
    if (index.column() != 0)
      continue;
    int row = playlists_->current()->proxy()->mapToSource(index).row();
    PlaylistItemPtr item(playlists_->current()->item_at(row));
    Song song = item->Metadata();

    if (song.IsEditable()) {
      songs << song;
      autocomplete_tag_items_ << item;
    }
  }

  track_selection_dialog_->Init(songs);
  tag_fetcher_->StartFetch(songs);

  track_selection_dialog_->show();
}

void MainWindow::AutoCompleteTagsAccepted() {
  foreach (PlaylistItemPtr item, autocomplete_tag_items_) {
    item->Reload();
  }

  // This is really lame but we don't know what rows have changed
  ui_->playlist->view()->update();
}
