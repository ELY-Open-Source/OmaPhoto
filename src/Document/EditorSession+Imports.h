// The import queue: files, Photoshop and RAW.
public:
    // Files decode off the UI thread; `done` follows this request.
    void importImages(const QList<QUrl> &urls, std::optional<QPointF> point = std::nullopt, std::function<void()> done = {});
    // The sheet shows while a Photoshop file is read.
    void beginPSDReading(const QString &title, const QString &confirmTitle);
    void finishPSDReading(const std::vector<PSDConversion> &conversions, std::function<void(bool)> answer);
    void endPSDReading();
    void finishConversion(bool confirmed);
    void insertPhotoshop(const PSDImport &imported, const QString &named, std::optional<QPointF> centeredAt = std::nullopt);
    // The RAW file the develop sheet shows, while it shows.
    const std::optional<RawDevelopRequest> &rawDevelop() const { return m_rawDevelop; }
    void decodeRaw(const QString &path, qint64 remaining);
    void developRaw(const QString &path, std::function<void(std::optional<RawDevelopSettings>)> answer);
    void finishRawDevelop(std::optional<RawDevelopSettings> settings);
    // Import pressed: the sheet stays until the layer is in.
    bool rawImporting() const { return m_rawImporting; }
    // Closes the sheet once the RAW is in, or cancelled.
    void endRawDevelop();

private:
    void drainImports();
    void decodeNext();
    qint64 remainingPixels() const;
    void decode(const QUrl &url, bool flattened);
    void finishDecode();
    void finishPhotoshopRead();
