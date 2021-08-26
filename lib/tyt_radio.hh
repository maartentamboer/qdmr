/** @defgroup tyt TYT/Retevis Radios
 * Abstract classes for TYT and Retevis radios.
 *
 * @ingroup dsc */
#ifndef TYT_RADIO_HH
#define TYT_RADIO_HH

#include "radio.hh"
#include "dfu_libusb.hh"

/** Implements an USB interface to TYT & Retevis radios.
 *
 * @ingroup tyt */
class TyTRadio: public Radio
{
	Q_OBJECT

public:
  /** Do not construct this class directly, rather use @c Radio::detect. */
  explicit TyTRadio(DFUDevice *device=nullptr, QObject *parent=nullptr);

  virtual ~TyTRadio();

public slots:
  /** Starts the download of the codeplug and derives the generic configuration from it. */
  bool startDownload(bool blocking=false);
  /** Derives the device-specific codeplug from the generic configuration and uploads that
   * codeplug to the radio. */
  bool startUpload(Config *config, bool blocking=false,
                   const CodePlug::Flags &flags = CodePlug::Flags());
  /** Encodes the given user-database and uploades it to the device. */
  bool startUploadCallsignDB(UserDatabase *db, bool blocking=false,
                             const CallsignDB::Selection &selection=CallsignDB::Selection());

protected:
  /** Thread main routine, performs all blocking IO operations for codeplug up- and download. */
	void run();

private:
  virtual bool connect();
  virtual bool download();
  virtual bool upload();
  virtual bool uploadCallsigns();

protected:
  /** The interface to the radio. */
	DFUDevice *_dev;
  /** Holds the flags to controll assembly and upload of code-plugs. */
  CodePlug::Flags _codeplugFlags;
  /** The generic configuration. */
	Config *_config;
  /** A weak reference to the user-database. */
  UserDatabase *_userDB;
};

#endif // UV390_HH
