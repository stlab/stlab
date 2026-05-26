---
layout: default
---

<div class="home">

  <h1>stlab libraries</h1>

  <p>Modern, modular C++ algorithms and data structures. Source and issues on
  <a href="https://github.com/stlab/stlab">GitHub</a>.</p>

  <table class='icon-table'>
  <tr>
    <td>
      <a href="{{ '/doxygen/' | relative_url }}">
        <div class='icon-box'>
          <div class='top'><i class="fa fa-inverse fa-book"></i></div>
          <div class='bottom'>API</div>
        </div>
      </a>
    </td>
    <td>
      <a href="{{ '/doxygen/' | relative_url }}group__stlab__concurrency.html">
        <div class='icon-box'>
          <div class='top'><i class="fa fa-inverse fa-random"></i></div>
          <div class='bottom'>Concurrency</div>
        </div>
      </a>
    </td>
    <td>
      <a href="{{ '/doxygen/' | relative_url }}group__stlab__forest.html">
        <div class='icon-box'>
          <div class='top'><i class="fa fa-inverse fa-tree"></i></div>
          <div class='bottom'>Forest</div>
        </div>
      </a>
    </td>
  </tr>
  </table>

  <br/>

{% assign releases = site.data.releases | sort: 'published_at' | reverse %}
{% assign release = releases | first %}
{% assign prev_release = releases | shift | first %}

  <h1>Latest Release: {{release.tag_name}}</h1>

Released: {{release.published_at | date: "%b %-d, %Y" }}
<br/>
Sources: <a href='https://github.com/stlab/stlab/compare/{{prev_release.tag_name}}...{{release.tag_name}}'>changes</a>&nbsp;&nbsp;|&nbsp;&nbsp;<a href='{{release.zipball_url}}'>zipball</a>&nbsp;&nbsp;|&nbsp;&nbsp;<a href='{{release.tarball_url}}'>tarball</a>
<br/>
Changes: {{release.body | markdownify}}
For older releases, the full list is available <a href='https://github.com/stlab/stlab/releases'>on GitHub</a>.

  <h1>Posts</h1>

  <ul class="post-list">
    {% for post in site.posts %}
      {% if post.categories contains 'release' %}
        {% continue %}
      {% endif %}
      {% if post.hidden %}
        {% continue %}
      {% endif %}
      <li>
        <span class="post-meta">{{ post.date | date: "%b %-d, %Y" }}</span>

        <h2>
          <a class="post-link" href="{{ post.url | prepend: site.baseurl }}">{{ post.title | markdownify }}</a>
        </h2>
      {{ post.excerpt | markdownify }}
      </li>
    {% endfor %}

  </ul>

  <p class="rss-subscribe">subscribe <a href='{{ "/feed.xml" | prepend: site.baseurl }}'>via RSS</a></p>

</div>